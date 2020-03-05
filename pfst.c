/*
 * fstest.c - A simple filesystem latency tester
 *
 * Copyright (c) 2020 Peter Eriksson <pen@lysator.liu.se>
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/wait.h>


char *argv0 = "fstest";
char *version = "1.4";

int f_timeout = 1000000;
int f_bufsize = 65536;
int f_sync = 0;
int f_verbose = 0;
int f_mkdir = 0;
int f_complex = 0;

unsigned long f_loops = 0;

/*
 * Calculate the difference between two struct timespec.
 * Returns elapsed time in microseconds, plus the 
 * elapsed time and a unit as a string.
 */
long
ts_delta(struct timespec *x,
	 const struct timespec *y,
	 long *res,
	 char **unit) {
  struct timespec r;
  
  /* Avoid overflow of r.tv_nsec */
  if (x->tv_nsec < y->tv_nsec) {
    x->tv_nsec += 1000000000L;
    x->tv_sec  -= 1;
  }
  
  r.tv_sec  = x->tv_sec - y->tv_sec;
  r.tv_nsec = x->tv_nsec - y->tv_nsec;
  
  if (unit && res) {
    if (r.tv_sec >= 600) {
      /* More than 10 minutes -> return minutes */
      *unit = "m";
      *res = r.tv_sec / 60;
    } else if (r.tv_sec >= 10) {
      /* More than 10 seconds - return seconds */
      *unit = "s";
      *res = r.tv_sec;
    } else if (r.tv_sec == 0) {
      if (r.tv_nsec < 10000) {
	/* Less than 10us - return nanoseconds */
	*unit = "ns";
	*res = r.tv_nsec;
      } else if (r.tv_nsec < 10000000) {
	/* Less than 10ms - return microseconds */
	*unit = "µs";
	*res = r.tv_nsec / 1000;
      } else {
	*unit = "ms";
	*res = r.tv_nsec / 1000000;
      }
    } else {
      *unit = "ms";
      *res = r.tv_sec * 1000 + r.tv_nsec / 1000000;
    }
  }
  
  /* Microseconds */
  return r.tv_sec * 1000000 + r.tv_nsec / 1000;
}




void
p_log(int e,
      struct timespec *t0,
      const char *fmt,
      ...) {
  time_t now;
  struct tm *tp;
  char tbuf[80];
  va_list ap;
  struct timespec t1;
  long tv = 0;
  long td = 0;
  char *us = NULL;
  
  
  if (t0) {
    clock_gettime(CLOCK_REALTIME, &t1);
    td = ts_delta(&t1, t0, &tv, &us);
  }

  if (!e && !f_verbose && td < f_timeout)
    return;

  time(&now);
  tp = localtime(&now);
  strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tp);
  fprintf(stderr, "%s [%4ld %-2s]: ", tbuf, tv, us);
  
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  if (e)
    fprintf(stderr, ": %s", strerror(e));

  if (td >= f_timeout)
    fprintf(stderr, " [Time limit exceeded]");

  putc('\n', stderr);
}



void
spin(void) {
  static char dials[] = "|/-\\";
  static int p = 0;
  static time_t t0;
  time_t t1;

  time(&t1);
  if (t0 != t1) {
    putc(dials[p], stderr);
    putc('\b', stderr);
    p = (p+1)%4;
    t0 = t1;
  }
}



int
start_test_simple(const char *path,
		  char *buf,
		  size_t bsize) {
  char *fn1 = "test1.dat";
  char *fn2 = "test2.dat";
  int rc = -1;
  int fd = -1;
  pid_t pid;
  struct timespec t0;
  unsigned long loop = 0;

  
  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "%s: Error: fork(): %s\n",
	    argv0, strerror(errno));
    exit(1);
  }
  
  if (!pid) {
    /* In child process */
    char subpath[256], *cp;

    signal(SIGINT, SIG_IGN);

    strcpy(subpath, "t-");
    gethostname(subpath+2, sizeof(subpath)-2);
    cp = strchr(subpath+2, '.');
    if (cp)
      *cp = '\0';
    else
      cp = subpath+strlen(subpath);
    sprintf(cp, "-%d", getpid());


    /* Change to main directory ---------------------------------------- */

    clock_gettime(CLOCK_REALTIME, &t0);
    rc = chdir(path);
    if (rc < 0) {
      p_log(errno, &t0, "chdir(\"%s\")", path);
      _exit(1);
    }
  

    while (!f_loops || loop < f_loops) {
      ++loop;

      if (f_verbose)
	fprintf(stderr, "\nLoop #%lu:\n", loop);

      /* Create temp directory ---------------------------------------- */

      clock_gettime(CLOCK_REALTIME, &t0);
      rc = mkdir(subpath, 0700);
      if (rc < 0) {
	p_log(errno, &t0, "%s: mkdir(\"%s\")", path, subpath);
	goto ErrorExit;
      }
      p_log(0, &t0, "%s: mkdir(\"%s\")", path, subpath);

    

      if (f_complex > 0) {
	/* Change to temp directory ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	rc = chdir(subpath);
	if (rc < 0) {
	  p_log(errno, &t0, "%s: chdir(\"%s\")", path, subpath);
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: chdir(\"%s\")", path, subpath);
	
	
	/* Open temp file ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	fd = open(fn1, O_CREAT|O_WRONLY, 0600);
	if (fd < 0) {
	  p_log(errno, &t0, "%s: open(\"%s\", WR)", path, fn1);
	  rc = fd;
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: open(\"%s\", WR)", path, fn1);
	
	
	/* Write temp file ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	rc = write(fd, buf, bsize);
	if (rc < 0) {
	  p_log(errno, &t0, "%s: write(\"%s\", ..., %lu)",
		path, fn1, (unsigned long) sizeof(buf));
	  goto ErrorExit;
	}
	if (rc != bsize) {
	  p_log(0, &t0, "%s: write(\"%s\", ..., %lu)=%d: Short write",
		path, fn1, (unsigned long) bsize, rc);
	  rc = -1;
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: write(\"%s\")", path, fn1);   
	
	
	
	/* Potentially sync temp file ---------------------------------------- */
	
	if (f_sync) {
	  clock_gettime(CLOCK_REALTIME, &t0);
	  rc = fsync(fd);
	  if (rc < 0) {
	    p_log(errno, &t0, "%s: fsync(\"%s\")", path, fn1);
	    goto ErrorExit;
	  }
	  p_log(0, &t0, "%s: fsync(\"%s\")", path, fn1);
	}
	
	
	/* Close temp file ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	rc = close(fd);
	if (rc < 0) {
	  p_log(errno, &t0, "%s: close(\"%s\")", path, fn1);
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: close(\"%s\")", path, fn1);
	
	
	/* Rename temp file ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	rc = rename(fn1, fn2);
	if (rc < 0) {
	  p_log(errno, &t0, "%s: rename(\"%s\", \"%s\")",
		path, fn1, fn2);
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: rename(\"%s\", \"%s\")", path, fn1, fn2);
	
	
	/* Open temp file for reading ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	fd = open(fn2, O_RDONLY, 0600);
	if (fd < 0) {
	  p_log(errno, &t0, "%s: open(\"%s\")",
		path, fn2);
	  rc = fd;
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: open(\"%s\", RD)", path, fn2);
	
	
	/* Read temp file ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	rc = read(fd, buf, bsize);
	if (rc < 0) {
	  p_log(errno, &t0, "%s: read(\"%s\", ..., %lu)",
		path, fn2, (unsigned long) sizeof(buf));
	  goto ErrorExit;
	}
	if (rc != bsize) {
	  p_log(0, &t0, "%s: read(\"%s\", ..., %lu)=%d: Short read",
		path, fn2, (unsigned long) bsize, rc);
	  rc = -1;
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: read(\"%s\", %d)=%d", path, fn2, bsize, rc);
	
	
	
	/* Close temp file ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	rc = close(fd);
	if (rc < 0) {
	  p_log(errno, &t0, "%s: close(\"%s\")",
		path, fn2);
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: close(\"%s\")", path, fn2);
	
	
	/* Delete temp file ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	rc = unlink(fn2);
	if (rc < 0) {
	  p_log(errno, &t0, "%s: unlink(\"%s\")", path, fn2);
	  goto ErrorExit;
	}
	p_log(0, &t0, "%s: unlink(\"%s\")", path, fn2);
	
	
	/* Go to parent directory ---------------------------------------- */
	
	clock_gettime(CLOCK_REALTIME, &t0);
	rc = chdir("..");
	if (rc < 0) {
	  p_log(errno, &t0, "%s: chdir(\"..\")", path);
	  _exit(1);
	}
	p_log(0, &t0, "%s: chdir(\"..\")", path);
      }

      /* Remove our temp directory ---------------------------------------- */

      clock_gettime(CLOCK_REALTIME, &t0);
      rc = rmdir(subpath);
      if (rc < 0) {
	p_log(errno, &t0, "%s: chdir(\"%s\")",
	       path, subpath);
	_exit(1);
      }
      p_log(0, &t0, "%s: rmdir(\"%s\")", path, subpath);

      sleep(1);
    }
  
    _exit(0);


  ErrorExit:
    if (fd >= 0)
      (void) close(fd);
    (void) unlink(fn1);
    (void) unlink(fn2);
    _exit(rc < 0 ? 1 : -1);
  }

  return pid;
}


int pidc = 0;
pid_t *pidv = NULL;


void
sigint_handler(int sig) {
  int i;

  fputs("\n*** Aborting: Please wait for tests to finish ***\n", stderr);

  for (i = 0; i < pidc; i++)
    if (pidv[i])
      kill(pidv[i], SIGTERM);

  sleep(1);

  for (i = 0; i < pidc; i++)
    if (pidv[i])
      kill(pidv[i], SIGKILL);
}


int
main(int argc,
     char *argv[]) {
  int i, j;
  char *cp;
  char *buf = NULL;
  char pfx;

  
  argv0 = argv[0];

  for (i = 1; i < argc && argv[i][0] == '-'; i++) {
    if (!argv[i][1])
      goto LastArg;
    
    for (j = 1; argv[i][j]; j++)
      switch (argv[i][j]) {
      case 'h':
	puts("Usage:");
	printf("  %s [<options>] <dir-1> [...<dir-N>]\n", argv[0]);
	puts("\nOptions:");
	puts("  -h         Display this");
	puts("  -s         fsync() all writes");
	puts("  -m         mkdir/rmdir <dir>");
	puts("  -c         Increase test complexity");
	puts("  -n <loops> Limit test loops");
	puts("  -b <size>  Buffer size to write/read [64k]");
	puts("  -t <time>  Test timeout [1s]");
	exit(0);
	
      case 'n':
	if (isdigit(argv[i][j+1]))
	  cp = argv[i]+j+1;
	else if (i+1 < argc && isdigit(argv[i+1][0]))
	  cp = argv[++i];
	else {
	  fprintf(stderr, "%s: Error: -n: Missing required loop count\n",
		  argv[0]);
	  exit(1);
	}
	if (sscanf(cp, "%lu%c", &f_loops, &pfx) < 1) {
	  fprintf(stderr, "%s: Error: %s: Invalid loop count\n",
		  argv[0], cp);
	  exit(1);
	}
	switch (toupper(pfx)) {
	case 'K':
	  f_loops *= 1000;
	  break;
	case 'M':
	  f_loops *= 1000000;
	  break;
	case 'G':
	  f_loops *= 1000000000;
	  break;
	case 0:
	  break;
	default:
	  fprintf(stderr, "%s: Error: %s: Invalid loop count\n",
		  argv[0], cp);
	  exit(1);
	}
	goto NextArg;

      case 't':
	if (isdigit(argv[i][j+1]))
	  cp = argv[i]+j+1;
	else if (i+1 < argc && isdigit(argv[i+1][0]))
	  cp = argv[++i];
	else {
	  fprintf(stderr, "%s: Error: -t: Missing required timeout\n",
		  argv[0]);
	  exit(1);
	}
	if (sscanf(cp, "%d%c", &f_timeout, &pfx) < 1) {
	  fprintf(stderr, "%s: Error: %s: Invalid timeout\n",
		  argv[0], cp);
	  exit(1);
	}
	switch (pfx) {
	case 's':
	  f_timeout *= 1000000;
	  break;
	  
	case 'm':
	  f_timeout *= 1000;
	  break;
	  
	case 'u':
	case 0:
	  break;

	default:
	  fprintf(stderr, "%s: Error: %s: Invalid timeout\n",
		  argv[0], cp);
	  exit(1);
	}
	goto NextArg;

      case 's':
	f_sync++;
	break;
	
      case 'v':
	f_verbose++;
	break;
	
      case 'm':
	f_mkdir++;
	break;
	
      case 'c':
	f_complex++;
	break;
	
      case 'b':
	if (isdigit(argv[i][j+1]))
	  cp = argv[i]+j+1;
	else if (i+1 < argc && isdigit(argv[i+1][0]))
	  cp = argv[++i];
	else {
	  fprintf(stderr, "%s: Error: -t: Missing required timeout\n",
		  argv[0]);
	  exit(1);
	}
	pfx = 0;
	if (sscanf(cp, "%d%c", &f_bufsize, &pfx) < 1) {
	  fprintf(stderr, "%s: Error: %s: Invalid buffer size\n",
		  argv[0], cp);
	  exit(1);
	}
	switch (toupper(pfx)) {
	case 'K':
	  f_bufsize *= 1024;
	  break;
	case 'M':
	  f_bufsize *= 1024*1024;
	  break;
	case 0:
	  break;
	default:
	  fprintf(stderr, "%s: Error: %s: Invalid buffer size\n",
		  argv[0], cp);
	  exit(1);
	}
	goto NextArg;

      case '-':
	goto LastArg;

      default:
	fprintf(stderr, "%s: Error: -%s: Invalid switch\n",
		argv[0], argv[i]+j);
	exit(1);
      }
  NextArg:;
  }

 LastArg:
  buf = malloc(f_bufsize);
  if (!buf) {
    fprintf(stderr, "%s: Error: malloc(%d): %s\n",
	    argv[0], f_bufsize, strerror(errno));
    exit(1);
  }

  if (i == argc) {
    fprintf(stderr, "%s: Error: Missing required <dir> argument\n",
	    argv[0]);
    exit(1);
  }

  signal(SIGINT, sigint_handler);

  if (f_verbose)
    printf("[fstest, version %s]\n", version);

  pidv = calloc(argc-i, sizeof(pidv[0]));
  if (!pidv) {
    fprintf(stderr, "%s: Error: calloc(%lu,%lu): %s\n",
	    argv[0], (unsigned long) argc-i, (unsigned long) sizeof(pidv[0]), strerror(errno));
    exit(1);
  }

  for (; i < argc; i++)
    pidv[pidc++] = start_test_simple(argv[i], buf, f_bufsize);

  while (1) {
    pid_t pid;
    int ws;

    if (!f_verbose)
      spin();

    while ((pid = waitpid(-1, &ws, WNOHANG)) < 0 && errno == EINTR)
      ;

    if (pid < 0) {
      if (errno == ECHILD) {
	exit(0);
      }

      fprintf(stderr, "%s: Error: wait(): %s\n", argv[0], strerror(errno));
      exit(1);
    }

    if (pid) {
      for (j = 0; j < pidc && pidv[j] != pid; j++)
	;
      if (j < pidc)
	pidv[j] = 0;
    }

    sleep(1);
  }
  
  exit(1);
}
