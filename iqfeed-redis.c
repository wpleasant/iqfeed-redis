/*  Copyright (C) 2014 - 2016    William G. Pleasant  */
    
/*   Early versions were based on and adapted from B. Wayne Lewis',
 *   www.illposed.net/bars  including the tcpserver and client not-to-mention
 *   the idea of using redis in the first place.
 */

/*    iqfeed-redis is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 2 of the License, or
 *    any later version.
 *    
 *    iqfeed-redis is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with iqfeed-redis.  If not, see <http://www.gnu.org/licenses/>.
 */



#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>

#include "hiredis/hiredis.h"

/****************************************************************/


#define VERSION 1.0
#define ABS 64 
#define BS  4096
#define BACKLOG 100
#define ctp 7778

// global fields for processing option -m  
//    -note these are protocol 5.1 names and will change !!!

static const char myFields51[] = 
"Symbol,"
"Message Contents,"
"Most Recent Trade Date,"
"Most Recent Trade TimeMS,"
"Bid TimeMS,"
"Ask TimeMS,"
"Most Recent Trade,"
"Most Recent Trade Size,"
"Bid,"
"Bid Size,"
"Ask,"
"Ask Size,"
"Number of Trades Today,"
"Total Volume,"
"TickID,"
"Most Recent Trade Conditions";

static const char myFields52[] = 
"Symbol,"
"Message Contents,"
"Most Recent Trade Date,"
"Most Recent Trade Time,"
"Bid Time,"
"Ask Time,"
"Most Recent Trade,"
"Most Recent Trade Size,"
"Bid,"
"Bid Size,"
"Ask,"
"Ask Size,"
"Number of Trades Today,"
"Total Volume,"
"TickID,"
"Most Recent Trade Conditions";

/* error printing */
#define PRINT_ERRNO(errno) fprintf(stderr,"%s\n",strerror(errno))
#define EPRINT(x) fprintf (stderr, "%s at %s, line %d.\n", x, __FILE__, __LINE__)
#define IQERR(x) do {       \
  fprintf (stderr, "%s at %s, line %d, exiting now.\n", x, __FILE__, __LINE__); \
  exit(0);        \
} while (0);


/* build an int repsentation of the protocol */
static unsigned int  PROTOCOL_ID (const char * x)
{ 
  if (!strncmp(x,"4.9",3))  return 49;
  if (!strncmp(x,"5.0",3))  return 50;
  if (!strncmp(x,"5.1",3))  return 51;
  if (!strncmp(x,"5.2",3))  return 52;
  return 49; /* iqfeed default */
}

/* A cheap string hack */
typedef struct  iqs 
{  int   len;
  char   buf[];
} iqs ;


static void signalHandler (int);

static void signalHandler (int sig)
{ /* catch termination signals and shut down gracefully */
  signal (sig, signalHandler);
  exit (0);
}

int tcpconnect (char *host, int port)
{
  struct hostent *h;
  struct sockaddr_in sa;
  int s;

  h = gethostbyname (host);
  if (!h)
  {
    s = -1;
  }
  else
  {
    s = socket (AF_INET, SOCK_STREAM, 0);
    bzero (&sa, sizeof (sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons (port);
    sa.sin_addr = *(struct in_addr *) h->h_addr;
    if (connect (s, (struct sockaddr *) &sa, sizeof (sa)) < 0)
    {
      close (s);
      return -11;
    }
  }
  return s;
}

int tcpserv (int port)
{
  int s, n, j;
  struct sockaddr_in sin;

  bzero (&sin, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl (INADDR_ANY); /* listen on all interfaces */
  sin.sin_port = htons (port);  /* OS assigns port if port=0 */

  s = socket (AF_INET, SOCK_STREAM, 0);
  if (s < 0)
  {
    perror ("socket");
    return -12;
  }
  n = 1;
  if (setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (const void *) &n, sizeof (n)) < 0)
  {
    perror ("setsockopt");
    close (s);
    return -13;
  }
  /* Set non-blocking */
  j = fcntl (s, F_GETFL, 0);
  if (j < 0)
  {
    perror ("Error setting non-blocking operation");
    close (s);
    return -14;
  }
  fcntl (s, F_SETFL, j | O_NONBLOCK);
  if (bind (s, (struct sockaddr *) &sin, sizeof (sin)) < 0)
  {
    close (s);
    return -15;
  }
  if (listen (s, BACKLOG) < 0)
  {
    close (s);
    return -16;
  }
  return s;
}


/* Helper function to format and send commands to iqfeed */
void iq_send_cmd(int sockid, int verbose, const char *format, ...) 
{
  char buf[BS*2];
  int  e = 0;
  
  va_list ap;
  va_start (ap, format);
  e += vsnprintf (buf,BS*2,format, ap);
  if ( e < 0) 
  { EPRINT("buffer overflow while sending msg to iqfeed");
    return;
  }
  va_end(ap);
  
  if (e > 1) 
  {
    if (!buf[e] != '\r')  buf[e++] = '\r';
    if (!buf[e] != '\n')  buf[e++] = '\n';
  }
  int r = write (sockid, (void *) buf, e);
  if (r == -1) PRINT_ERRNO(errno);
  if (verbose) fprintf(stderr," iq_send_cmd: %.*s",e, buf);
}

/*
   Reads lines from a file "fn"; sending each line to the iqfeed socket "sockid"
   using "format".
*/

void iq_send_file (int sockid, int verbose, const char * fn, const char * format)
{
  FILE * sym_f = fopen (fn, "r");
  if (sym_f)
  { 
    char * sym_l = NULL;
    size_t tmplen = 0;
    int k;
    while ((k = getline (&sym_l, &tmplen, sym_f)) != -1)
    {
      if (k == 0 ) continue;
      iq_send_cmd(sockid, verbose,format,sym_l);
    }
    if (sym_l)
      free (sym_l);
    fclose (sym_f);
  }
}


/* allocate a new iqs string the size of BS */
iqs * alloc_iqs (iqs * s)
{
  if (!s)  s = (iqs *) calloc(1,sizeof(iqs)+BS);
  if (!s)  IQERR("error allocating iqs string"); 
  return s;
}

void free_iqs (iqs * s)
{ 
  if (!s) return;
  free(s);
}

inline void clear_iqs (iqs * s) 
{
  s->len    = 0;
  s->buf[0] = '\0';
}


/*
   Here we take a char buffer (ibuf) that has been filled by read()
   and copy each newline found into the array of iqs strings (**p); also setting
   the iqs->len. If the iqs string has not been cleared using clear_iqs() then
   the line will be appended to the end of the allready present string.
   If an incomplete string is found then we copy it to the next open iqs string
   (setting incompIDX) which is then later moved back to the first position 
   in the array where we can then append the incomplete data when it has been 
   received.
*/

static inline int split_iqs (char *ibuf, iqs **p, int lenread, int * incompIDX)
{ 
  char * tt;
  char * tbuf = ibuf;
  int   lloc = 0, i = 0, tlen = 0;
  do 
  { tt  = (char *) memchr((void *) tbuf+lloc, '\n', lenread-lloc);
    if (tt)
    { 
      iqs * s = p[i];
      if (!s) IQERR("iqs array was too small, shutting down"); 
      tlen = tt-(tbuf+lloc)+1;
      memcpy(s->buf+s->len,tbuf+lloc,tlen); /*append string if s->len */
      s->len += tlen-1;
      s->buf[s->len+1] = '\0';
      lloc += tlen; /* one past '\n' */
      ++i;
    }
  } while (tt != NULL && lloc < lenread && i < ABS-1 ); 

  if (lloc<lenread)
  { /* Copy incomplete line to the next position and mark its location */
    iqs * s = p[i+1];
    tlen = tbuf+lenread - (tbuf+lloc)+1;
    memcpy(s->buf+s->len,tbuf+lloc,tlen);
    s->len += tlen-1;
    s->buf[s->len+1] = '\0';
    *incompIDX = i;
  } else 
  { 
    *incompIDX = 0;
  }
  return i;
}



inline int  q_push (iqs *, redisContext *, unsigned int);
inline int  r_push (iqs *, redisContext *, unsigned int);
inline int  g_push (iqs *, const char *, redisContext *, unsigned int);
inline void t_push (iqs *, const char *, redisContext *, unsigned int);

static const char * helpmsg =
"\nOptions: iqfeed-redis [-h]\n" 
" [-I <iqfeedhost>]\n"
" [-P <iqfeedport>]\n" 
" [-H <redishost>]\n"
" [-X <redisport>]\n"
" [-F <unix_file_for_redis>]\n"
" [-Q <iqfeed protocol>]\n"
" [-f flag - use default redis unix connection /tmp/redis.sock]\n"
" [-m flag - use my default_fields]\n"
" [-t flag - record all messages to .tape]\n"
" [-n flag - turn news on]\n"
" [-k flag - keep <LF> on each message]\n\n";

int main (int argc, char **argv)
{

  /* Set up signal handlers */
  signal (SIGHUP,  signalHandler);
  signal (SIGINT,  signalHandler);
  signal (SIGQUIT, signalHandler);
  signal (SIGILL,  signalHandler);
  signal (SIGTRAP, signalHandler);
  signal (SIGFPE,  signalHandler);
  signal (SIGKILL, signalHandler);
  signal (SIGTERM, signalHandler);


  int s, p, j, k, go, t, u, c, i;
  unsigned int rmlf = 1, newson = 0, useunixfile = 0;
  unsigned int recordtape = 0, verbose = 1;
  //struct timeval tv;
  struct pollfd pfds[2];
  struct sockaddr_in sa;
  struct sockaddr_in sin;
  socklen_t slen;
  char *ibuf;
  char *obuf;
  int bs  = BS;
  int abs = ABS;
  redisContext *redis;
  
  char *redisFilePath = (char * )"/tmp/redis.sock";
  char *redisHost     = (char * )"127.0.0.1";
  int   redisPort     = 6379;
  int   iqfPort       = 5009;
  char  *iqfHost      = (char *)"127.0.0.1";
  char  *pp           = (char *)"5.2";
  int   useMyFields   = 0;

  ibuf  = (char *) malloc (bs * sizeof (char));
  if (!ibuf)   IQERR("allocation error "); 
  obuf = (char *) malloc (bs * sizeof (char));
  if (!obuf)   IQERR("allocation error"); 


  /* allocate an array of strings here.. */
  iqs **sbs  = calloc(abs, sizeof(iqs *));
  if (!sbs)  IQERR("error allocating iqs array"); 
  for (i=0;i<abs;i++)
    sbs[i] =  alloc_iqs(sbs[i]);


  /* Parse command-line options */
  while ((c = getopt (argc, argv,"hI:P:H:X:Q:F:ftmnk")) != -1)
    switch (c)
    {
      case 'h':
        printf("\n iqfeed-redis version %f\n\n %s",VERSION,helpmsg);
        exit (0);
        break;
      case 'I':
        iqfHost = optarg;
        break;
      case 'P':
        iqfPort = atoi (optarg);
        break;
      case 'H':
        redisHost = optarg;
        break;
      case 'X':
        redisPort = atoi(optarg);
        break;
      case 'F':
        redisFilePath = optarg;
        ++useunixfile;
        break;
      case 'Q':
        pp      = optarg; 
        break;
      case 'f':
        ++useunixfile;
        break;
      case 'm':
        ++useMyFields;
        break;
      case 'n':
        ++newson;
        break;
      case 't':
        ++recordtape;
        break;
      case 'k':
        rmlf = 0;
        break;
      default:
        fprintf(stderr, "Invalid argument: %c\n", c);
        break;
    }

  /* remove the '\n's default is yes */
  const unsigned int rmLF = rmlf; 
  const unsigned int recordTape = recordtape; 
  const unsigned int ppid = PROTOCOL_ID(pp);

  /* Set up a server for control messages */
  p = ctp;
  t = tcpserv (p);
start:
  bzero (&sin, sizeof (sa));
  slen = sizeof (sin);
  go = 1;

  bzero (ibuf, bs);
  bzero (obuf, bs);

  if (verbose) fprintf(stderr,"\n");
  /* connect to redis */
  if (useunixfile)
  { fprintf (stderr," Connecting to redis using %s\n",redisFilePath);
    redis = redisConnectUnix(redisFilePath);
  } else 
  { fprintf (stderr," Connecting to redis on host %s and port %d\n",redisHost,redisPort);
    redis = redisConnect (redisHost, redisPort);
  }
  if (redis->err)
  {
    fprintf (stderr, " Can't connect to Redis...terminating.\n");
    raise (SIGTERM);
  }

  /* Connect to the IQFeed level 1 service */
  s = tcpconnect (iqfHost, iqfPort);
  if (s < 0)
    goto end;
  fprintf (stderr, " Connected to the IQ feed, ticks control port is %d\n",ctp);
  
  /* Set Protocol */
  if (pp || strlen(pp)>2) 
    iq_send_cmd(s, verbose,"S,%s,%s", "SET PROTOCOL",pp); 

  /*  Set Fields */
  if (useMyFields) 
  { if (ppid == 51)
      iq_send_cmd(s, verbose, "S,%s,%s","SELECT UPDATE FIELDS",myFields51);
    else 
    if (ppid > 51)
      iq_send_cmd(s, verbose, "S,%s,%s","SELECT UPDATE FIELDS",myFields52);
  } else 
  {
    fprintf(stderr," Reading fields from file\n");
    iq_send_file (s,verbose,"/usr/local/etc/iqfeed.fields", "S,SELECT UPDATE FIELDS,%s");
  }

  /* Parse the symbol watch file if it exists: */
  iq_send_file (s,verbose,"/usr/local/etc/iqfeed.symbols", "w%s");

 
  /* Turn news on */
  if (newson) iq_send_cmd(s,verbose,"S,%s","NEWSON");
  
  /* Request all update field names */
  iq_send_cmd(s, verbose,"S,%s","REQUEST ALL UPDATE FIELDNAMES");


  /* Set up polling */
  pfds[0].fd = s;
  pfds[0].events = POLLIN | POLLPRI;
  pfds[1].fd = t;
  pfds[1].events = POLLIN;

  int jj, ii, incompIDX = 0;
  unsigned long long mcount = 0;
  while (go)
  {
    p = poll (pfds, 2, 500);
    if (p < 1)
    {
      continue;
    }
    if (pfds[0].revents & POLLIN)
    {
      j = read(s, (void *) ibuf, bs-sbs[0]->len);
      if (j > 0)
      {
        //gettimeofday(&tv,NULL); /// append to each string ???
        jj = split_iqs(ibuf, sbs, j, &incompIDX);
        for (ii=0; ii<jj; ii++)
        { 
          iqs * msg = sbs[ii];
          int A = msg->buf[0];
          if (recordTape)
            t_push (msg, ".tape", redis, rmLF);
          switch(A) {
            case 'Q' :
              q_push (msg, redis, rmLF);
              break;
            case 'N' :
              g_push (msg, ".news",redis, rmLF);
              break;
            case 'T' :
              g_push (msg, ".time", redis, rmLF);
              break;
            case 'S' :
              g_push (msg, ".sys", redis, rmLF);
              break;
            case 'P' :
            case 'F' :
            case 'R' :
              r_push (msg, redis, rmLF);
              break;
            case 'E' :
              g_push (msg, ".err", redis, rmLF);
              break;
            case 'n' :
              g_push (msg, ".notfound", redis, rmLF);
              break;
            default :
              g_push (msg, ".others", redis, rmLF);
              break;
          }
          clear_iqs(msg);
        }
        mcount += jj;
        if(incompIDX) 
        { /* swap incomplete string to first position in sbs*/
          iqs * tmpiqs = sbs[0];
          sbs[0]  = sbs[incompIDX];
          sbs[incompIDX] = tmpiqs;
        }
        bzero(ibuf, j);
      }
      else
      {
        goto end;
      }
    }
    else if (pfds[1].revents & POLLIN)
    {
      u = accept (t, (struct sockaddr *) &sa, &slen);
      j = read (u, (void *) obuf, bs);
      if (j > 0)
      {
        k = write (s, (void *) obuf, j);
        if (k == -1) EPRINT(" Error writing to iqfeed level 1 buffer");
        /// fprintf(stderr, "Sending %s to IQ Feed\n",obuf);
        if (strncmp (obuf, "S,DISCONNECT", 3) == 0)
        {
          goto end;
        }
        bzero (obuf, bs);
      }
      close (u);
    }
  }
end:
  fprintf (stderr, "IQ feed connection lost. Re-initializing...\n");
  close (s);
  sleep (5);
  goto start;

  /* Never get here  */
  if (ibuf)  free(ibuf);
  if (obuf)  free(obuf);
  for (i=0;i<abs;i++)  if (sbs[i]) free(sbs[i]);
  if (sbs) free(sbs);
  return 0;
}


/* Functions to make symbol names for redis from iqfeed... no copying here */
/*
   q_push takes a level 1 message from iqfeed and makes a redis symbol by
   removing the first two bytes in the message (normally 'Q,') using only the
   symbol name "from the first comma to the second". The message sent
   is then trunckated sending only the data after the symbol.
   rmLF == 1 removes the trailing '\n' otherwise set it to 0;
*/

inline int q_push ( iqs * m, redisContext *redis, unsigned int rmLF)
{
  char*   buf  = m->buf;
  char*   tail = (char *) memchr((void *) buf+2, ',', 8);
  if (!tail) return 0;
  size_t  ot = (size_t)(tail-buf-2);
  redisReply * reply = redisCommand (redis, "RPUSH %b %b", buf+2, ot, buf+ot+3, (size_t)m->len-ot-2-rmLF);
  if (reply) free(reply);
  return 1;
}

/*
   r_push takes a level 1 message from iqfeed and makes a redis symbol by
   changing the first comma to '.' sending the head of the string 
   including the the message type and symbol name together e.g. "P.XOM" or "F.SPY".
   The message sent is then trunckated by only the first two bytes, sending
   the iqfeed symbol with the rest of the string.
   rmLF == 1 removes the trailing '\n' otherwise set it to 0;
*/

inline int r_push(iqs * m, redisContext * redis, unsigned int rmLF)
{
  char*   buf  = m->buf;
  char*   tail = (char *) memchr((void *) buf+2, ',', 8);
  if (!tail) return 0;
  size_t  ot   = (size_t)(tail-buf-2);
  buf[1] = '.';
  redisReply * reply = redisCommand (redis, "RPUSH %b %b", buf, ot+2, buf+2, (size_t)m->len-1-rmLF);
  if (reply) free(reply);
  return 1;
}

/*
   g_push takes a level 1 message from iqfeed and makes a redis symbol from *sym
   The message sent is then trunckated by only the first two bytes, sending
   the rest of the string.
   rmLF == 1 removes the trailing '\n' otherwise set it to 0;
*/

inline int g_push (iqs * m, const char *sym, redisContext *redis, unsigned int rmLF)
{
  char*   buf  = m->buf;
  redisReply * reply = redisCommand (redis, "RPUSH %s %b", sym, buf+2, (size_t)m->len-1-rmLF);
  if (reply) free(reply);
  return 1;
}

/*  t_push sends the whole string to sym */
inline void t_push(iqs * m, const char *sym,  redisContext *redis, unsigned int rmLF)
{
  redisReply * reply = redisCommand (redis, "RPUSH %s %b",sym,m->buf,m->len+1-rmLF);
  if (reply) free(reply);
}


