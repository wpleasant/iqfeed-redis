##iqfeed-redis
========
iqfeed-redis is a simple and fast IQFeed client which stores level-I 
messages in redis.

### Dependencies

- [redis](http://redis.io)
- [hiredis](https://github.com/redis/hiredis), C library for Redis 
- [IQFeed](http://www.iqfeed.net)

### Getting Started

If hiredis is installed use 

- make clean 
- make
- make install

These should work but if there are linking problems just copy the latest version of
hiredis from https://github.com/redis/hiredis into iqfeed-redis folder and use

- make cleanlocal
- make local
- make install 

### Options

After starting iqfeed.exe, in linux "wine iqconnect.ext -product..." run iqfeed-redis
with your preferred options. Type iqfeed-redis -h to see a list of options.

- I <iqfeedhost>  the default is 127.0.0.1
- P <iqfeedport>  the default 5009
- H <redishost> the default is 127.0.0.1
- X <redisport> the default is 6379
- F <unixfile>  if redis.conf is setup to take a non-default unixfile set it here
- Q <protocol>  the current default is 5.1 but can be changed to 4.9, 5.0, 5.1, or 5.2
- f flag - uses the default redis unix connection "/tmp/redis.sock". -check redis.config
- m flag - use mydefaultfields (a hardcoded set of field names which is likely to change)
- t flag - record all messages to .tape, can be used to collect all messages into one list
- n flag - turn news on
- k flag - keep <LF> or '\n' on each message, the default is to remove the training '\n'

If either of the unix-file connection flags are set they override tcp connection flags to redis...
it is faster.

### On Start

After starting iqfeed-redis it connects a tcp client to iqfeed and sets up tcp server on
port 7778 to send system messages to iqfeed. before any messages are received it 

- Sets iqfeed's protocol.
- If the m flag is not selected, it looks for the file "/usr/local/etc/iqfeed.fields" and
    if found it sends request to UPDATE FIELD NAMES. The file must be a single row csv.
- It looks for the file "/usr/local/etc/iqfeed.symbols" and if found it send request for 
    each symbol found. Each row must contain only one symbol and no other characters.
- A request for news is sent if the n flag is used.
- A request for ALL UPDATE FIELDNAMES is sent.

### Control Port

To use the control port to request and/or remove symbols you could  "echo wSPY | nc localhost 7778"
and "echo rXOM | nc localhost 7778" or set SPY to only receive trades "echo tSPY | nc localhost 7778"

### Redis Key and Message Formats

All iqfeed level I messages start with a char identifier followed by a comma which are removed by 
iqfeed-redis after parsing. The only excption is if the t flag is selected and all messages copied
to a single list before parsing.

- **Q messages** Quotes and trades
  - **redis key** the symbol sent from iqfeed e.g.  SPY, MSFT, IBM
  - **redis msg** the first two bytes and the symbol are dropped from the messages.
- **F P R messages** Fundamental, Summary, and Regional
  - **redis key** the symbol name is appended the the message type and the comma is replaced
    by '.' e.g.  P.SPY , F.MSFT, R.XX
  - **redis msg** just the first two bytes are dropped from the messages, keeping the symbol name.
- **N messages**  News
  - **redis key** all news messages are pushed into ".news" 
  - **redis msg** just the first two bytes are dropped from the messages.
- **S messages**  System
  - **redis key** all system messages are pushed into ".sys" 
  - **redis msg** just the first two bytes are dropped from the messages.
- **T messages**  Timestamps
  - **redis key** all timestamp messages are pushed into ".time"
  - **redis msg** just the first two bytes are dropped from the messages.
- **E messages**  Errors
  - **redis key** all error messages are pushed into ".err" 
  - **redis msg** just the first two bytes are dropped from the messages.
- **n messages**  Symbols not found
  - **redis key** all symbol not found messages are pushed into ".notfound"
  - **redis msg** just the first two bytes are dropped from the messages.

### AUTHOR

William Pleasant williampleasant@gmail.com based on work by 
[B. W. Lewis](https://github.com/bwlewis),[illposed.net/bars](http://illposed.net).

## License

GPL (>= 2)

