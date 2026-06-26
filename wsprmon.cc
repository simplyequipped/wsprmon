//
// wsprmon: streaming WSPR receiver. Captures aligned 2-minute slots (or reads
// .wav files), feeds 12 kHz audio to the stock wsprd decoder, and streams
// decodes to stdout in an ft8mon-style format.
//
// Howard, simplyequipped
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <string>
#include <vector>
#include <signal.h>
#ifdef __linux__
#include <sys/prctl.h>   // PR_SET_PDEATHSIG: avoid being orphoned
#endif
#include "snd.h"
#include "util.h"

static const int RATE = 12000;          // wsprd is hard-wired to 12 kHz
static const int CAPTURE = 114;         // seconds wsprd reads from the wav
static const int CYCLE = 120;           // WSPR slot length, aligned to even minutes

static void
usage()
{
  fprintf(stderr, "Usage: wsprmon -card card channel [-f dial_MHz] [-hz] [-a workdir] [-wsprd path]\n");
  fprintf(stderr, "       wsprmon [-f dial_MHz] [-hz] -file file.wav ... [-wsprd path]\n");
  fprintf(stderr, "       wsprmon -list\n");
  fprintf(stderr, "  -hz: report frequency in Hz instead of MHz\n");
  fprintf(stderr, "  wsprd binary: -wsprd, else $WSPRD, else PATH, else next to wsprmon\n");
  exit(1);
}

// integer-decimate to 12 kHz with a windowed-sinc anti-alias filter
static std::vector<double>
to_12k(const std::vector<double> &in, int rate)
{
  if(rate == RATE)
    return in;
  if(rate % RATE != 0){
    fprintf(stderr, "wsprmon: sample rate %d is not a multiple of %d\n", rate, RATE);
    exit(1);
  }
  int m = rate / RATE;
  int n = 32 * m + 1;
  double fc = 5400.0 / rate;
  std::vector<double> h(n);
  double sum = 0;
  for(int i = 0; i < n; i++){
    int k = i - n / 2;
    double s = (k == 0) ? 2 * fc : sin(2 * M_PI * fc * k) / (M_PI * k);
    double w = 0.54 - 0.46 * cos(2 * M_PI * i / (n - 1));
    h[i] = s * w;
    sum += h[i];
  }
  for(double &x : h)
    x /= sum;

  std::vector<double> out;
  out.reserve(in.size() / m + 1);
  int half = n / 2;
  for(int j = 0; j < (int) in.size(); j += m){
    double acc = 0;
    for(int i = 0; i < n; i++){
      int idx = j - half + i;
      if(idx >= 0 && idx < (int) in.size())
        acc += h[i] * in[idx];
    }
    out.push_back(acc);
  }
  return out;
}

// point wsprd's growing output files at /dev/null; keep hashtable.txt
static void
setup_workdir(const std::string &dir)
{
  if(access(dir.c_str(), W_OK) != 0){
    fprintf(stderr, "wsprmon: working directory not writable: %s (use -a <dir>)\n",
            dir.c_str());
    exit(1);
  }
  const char *grow[] = { "ALL_WSPR.TXT", "wspr_spots.txt", "wspr_timer.out", 0 };
  for(int i = 0; grow[i]; i++){
    std::string p = dir + "/" + grow[i];
    unlink(p.c_str());
    if(symlink("/dev/null", p.c_str()) != 0)
      perror(p.c_str());
  }
}

static std::string g_wsprd = "wsprd";   // -wsprd, else $WSPRD, else PATH
static bool g_hz = false;               // -hz: emit freq in Hz instead of MHz

// write the 12 kHz window, run wsprd, restream its decodes with our timestamp
static void
decode_and_emit(const std::vector<double> &s12, double dial,
                const std::string &dir, int hh, int mm)
{
  std::string wav = dir + "/wsprmon.wav";
  writewav(s12, wav.c_str(), RATE);

  char cmd[512];
  snprintf(cmd, sizeof cmd, "cd '%s' && '%s' -f %.6f -J wsprmon.wav 2>/dev/null",
           dir.c_str(), g_wsprd.c_str(), dial);

  int decodes = 0;
  FILE *p = popen(cmd, "r");
  if(p){
    char line[256];
    while(fgets(line, sizeof line, p)){
      if(strstr(line, "<DecodeFinished>"))
        break;
      char tm[16], msg[160];
      float snr, dt;
      double freq;                       // double so Hz keeps full precision
      int drift;
      if(sscanf(line, "%15s %f %f %lf %d %159[^\n]",
                tm, &snr, &dt, &freq, &drift, msg) == 6){
        if(g_hz)
          printf("%02d%02d00 %3.0f %4.1f %11.1f %2d  %s\n",
                 hh, mm, snr, dt, freq * 1e6, drift, msg);
        else
          printf("%02d%02d00 %3.0f %4.1f %10.6f %2d  %s\n",
                 hh, mm, snr, dt, freq, drift, msg);
        decodes++;
      }
    }
    pclose(p);
  }
  printf("%02d:%02d:00 decodes: %d\n", hh, mm, decodes);
  fflush(stdout);
}

static void
run_live(SoundIn *sin, double dial, const std::string &dir)
{
  sin->start();
  int rate = sin->rate();

  while(1){
    double tt = now();
    long long cyc = (long long) tt - ((long long) tt % CYCLE);
    if(tt - cyc >= CAPTURE + 2){
      double t0;
      std::vector<double> s = sin->get(CYCLE * rate, t0, 1);
      double tend = now() - sin->latency();   // wall clock, latency-corrected;
                                               // ADC timestamp unreliable on raw-hw backend
      long long cstart = ((long long) (tend / CYCLE)) * CYCLE;
      long long nstart = s.size() - (long long) (rate * (tend - cstart));

      if(nstart >= 0 && nstart + (long long) CAPTURE * rate <= (long long) s.size()){
        std::vector<double> win(s.begin() + nstart,
                                s.begin() + nstart + CAPTURE * rate);
        time_t t = cstart;
        struct tm g;
        gmtime_r(&t, &g);
        decode_and_emit(to_12k(win, rate), dial, dir, g.tm_hour, g.tm_min);
      }
      sleep(6);
    }
    usleep(200 * 1000);
  }
}

// derive HH:MM from a WSJT-style name like 150426_0918.wav, else 00:00
static void
name_time(const std::string &path, int &hh, int &mm)
{
  hh = mm = 0;
  size_t u = path.rfind('_');
  if(u != std::string::npos && path.size() >= u + 5){
    std::string t = path.substr(u + 1, 4);
    if(t.size() == 4 && isdigit(t[0]) && isdigit(t[3])){
      hh = (t[0] - '0') * 10 + (t[1] - '0');
      mm = (t[2] - '0') * 10 + (t[3] - '0');
    }
  }
}

// is `name` an executable reachable via $PATH?
static bool
on_path(const std::string &name)
{
  const char *path = getenv("PATH");
  if(!path)
    return false;
  std::string p = path;
  for(size_t s = 0; s <= p.size(); ){
    size_t e = p.find(':', s);
    if(e == std::string::npos)
      e = p.size();
    if(e > s && access((p.substr(s, e - s) + "/" + name).c_str(), X_OK) == 0)
      return true;
    s = e + 1;
  }
  return false;
}

// directory containing the running wsprmon binary
static std::string
self_dir(const char *argv0)
{
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  std::string s = (n > 0) ? (buf[n] = 0, std::string(buf)) : std::string(argv0 ? argv0 : "");
  size_t sl = s.rfind('/');
  return sl == std::string::npos ? "" : s.substr(0, sl);
}

// can g_wsprd actually be exec'd? a path is checked directly; a bare name
// (relying on $PATH) is looked up on $PATH.
static bool
wsprd_runnable(const std::string &w)
{
  if(w.find('/') != std::string::npos)
    return access(w.c_str(), X_OK) == 0;
  return on_path(w);
}

int
main(int argc, char *argv[])
{
#ifdef __linux__
  // If parent exits for any reason, even a hard kill that skips its
  // teardown, the kernel sends SIGTERM
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  if(getppid() == 1) return 0;
#endif
  const char *card = 0, *chan = "0", *cli_wsprd = 0;
  double dial = 0;
  std::string dir;
  bool dir_set = false;
  std::vector<std::string> files;
  bool list = false;

  for(int i = 1; i < argc; i++){
    std::string a = argv[i];
    if(a == "-list"){
      list = true;
    } else if(a == "-card" && i + 2 < argc){
      card = argv[++i];
      chan = argv[++i];
    } else if(a == "-f" && i + 1 < argc){
      dial = atof(argv[++i]);
    } else if(a == "-a" && i + 1 < argc){
      dir = argv[++i];
      dir_set = true;
    } else if(a == "-wsprd" && i + 1 < argc){
      cli_wsprd = argv[++i];
    } else if(a == "-hz"){
      g_hz = true;
    } else if(a == "-file"){
      while(i + 1 < argc)
        files.push_back(argv[++i]);
    } else {
      usage();
    }
  }

  if(cli_wsprd){
    g_wsprd = cli_wsprd;
  } else if(const char *e = getenv("WSPRD")){
    g_wsprd = e;
  } else if(!on_path("wsprd")){
    std::string co = self_dir(argv[0]) + "/wsprd";
    if(access(co.c_str(), X_OK) == 0)
      g_wsprd = co;
  }

  if(list){
    snd_list();
    return 0;
  }

  // -f is optional. Without it the dial stays 0, so wsprd reports the audio offset
  // instead of an absolute frequency. We still need a source, either -file or -card.
  if(files.empty() && !card)
    usage();

  // Fail fast if wsprd can't be run: otherwise every slot silently popen()s a
  // missing command and reports "decodes: 0" forever, indistinguishable from a
  // dead-quiet band. After the usage() checks, so -h/no-args still show help.
  if(!wsprd_runnable(g_wsprd)){
    fprintf(stderr,
            "wsprmon: wsprd not found or not executable: '%s'\n"
            "  specify with -wsprd <path>, $WSPRD, on $PATH, or next to wsprmon\n",
            g_wsprd.c_str());
    return 1;
  }

  // default to a private temp directory so wsprmon never depends on (and can't
  // be killed by) a non-writable working directory, e.g. CWD=/ under systemd.
  if(!dir_set){
    char tmpl[] = "/tmp/wsprmon.XXXXXX";
    if(mkdtemp(tmpl) == 0){
      perror("wsprmon: mkdtemp");
      return 1;
    }
    dir = tmpl;
  }

  setup_workdir(dir);

  if(!files.empty()){
    for(auto &f : files){
      int rate;
      std::vector<double> s = readwav(f.c_str(), rate);
      int hh, mm;
      name_time(f, hh, mm);
      decode_and_emit(to_12k(s, rate), dial, dir, hh, mm);
    }
    return 0;
  }

  if(!card)
    usage();
  SoundIn *sin = SoundIn::open(card, chan, RATE);
  run_live(sin, dial, dir);
  return 0;
}
