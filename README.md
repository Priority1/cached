**cached** is a daemon for caching big files(video, in most cases) from backends to frontends.

## How it works

Daemon works at frontends, looks into logfile of requests that passed to backend
and download popular files to frontends.
It uses MySQL database to keep scoreboard and a table with list of files, that already cached on frontends.

Depending on your architecture it can work in two modes(dmode):
*  Round-Robin - download all popular files on all frontends.
*  Sharding - download files only whith my fe_id setting.

## Installation
Requirement packages:
* Mysql client libs
* Pcre (pcreposix)
* Libcurl
* Gamin(fam)

Download `cached-0.4.tar.gz`

On the frontend server:
```
tar -xzvf cached-0.4.tar.gz
cd cached-0.4
./configure
make
make install
```
On MySQL server:
```
mysql < cached.sql
```

## Configuration

/etc/cached/cached.conf:
```
# PID file location
pid = "/var/run/cached.pid";

# user
user = "www-data";

# Frontend ID
fe_id = 77;

# if file requests >= cache_req_count, file will be cached
cache_req_count = 10;

# Download workers
workers = 3;

# Pass a long as parameter containing the maximum time in seconds 
# that you allow the libcurl transfer operation to take. 
curl_timeout = 600;

# Connection time out, in seconds 
curl_connect_timeout = 10;

# Log
log = "/var/log/cached/cached.log";

# LogLevel
# 1 - ERROR, 2 - WARN, 3 - DEBUG
log_level = 3;

# access log for processing
access_log = "/var/log/nginx/proxypass.log";

# cached-video path (without "/" on end)
cache_store = "/tmp/nginx";

# Backends
upstreams = [ "10.100.1.61",
          "192.168.100.150",
              "10.100.1.63"];

# if upstream failed, it will be marked as dead on upstream_dead_timeout seconds.
upstream_dead_timeout = 60;

# Logfile regex (in pcre format)
# Matches:
# 1 - datetime in msec
# 2 - size
# 3 - last_modified
# 4 - status
# 5 - file path
log_regex = "(\\d+)\\.\\d+\\|(\\d+)\\|(.+)\\|(200|206|304|499)\\|GET \\/(\\S+\.flv|\\S+.mp4).*";

# Last_modififed date format (in MySQL from_str format)
lm_format = "%a, %d %b %Y %H:%i:%S GMT";

# MySQL settings
mysql_host = "somemysqlserver";
mysql_port = 3306;
mysql_db   = "cached";
mysql_login = "cached";
mysql_password = "cached";

# Download mode
# 1 - download only my videos (SHARDING MODE)
# 2 - download all popular (ROUND ROBIN MODE)

dmode = 1;
```

/etc/nginx/nginx.conf:
```
...
        location ~ \.mp4 {
               root    /tmp/nginx;
               open_file_cache_errors  off;
               error_page      404 = @getmp4;
               }

        location @getmp4 {
               proxy_pass http://10.100.1.61;
               error_page      404 = /errors/404.html;
                access_log  /var/log/nginx/proxypass.log vcache_new;
               }

...
```

## Running
In our example we have 3 backend servers(10.100.1.61 ...), so we need to run Web-server on, at least, one backend.
Run nginx on frontend and start cached:
```
/etc/init.d/cached start
```

Here what we got in logfile:
```
tail -f /var/log/cached/cached.log
[Sat Jan  5 01:51:31 2013] [ERROR] (master)4986# All workers started
[Sat Jan  5 01:51:31 2013] [ERROR] (downloader)4989# Connected to MySQL.
[Sat Jan  5 01:51:31 2013] [DEBUG] (downloader)4989# Upstream: 10.100.1.61, active: 1
[Sat Jan  5 01:51:31 2013] [ERROR] (fparser)4987# Connected to MySQL.
[Sat Jan  5 01:51:31 2013] [DEBUG] (downloader)4989# Upstream: 10.100.1.62, active: 1
[Sat Jan  5 01:51:31 2013] [DEBUG] (fparser)4987# FAMExists
[Sat Jan  5 01:51:31 2013] [DEBUG] (fparser)4987# Wow, fresh meat...(Gamin)
[Sat Jan  5 01:51:31 2013] [DEBUG] (fparser)4987# FAMEndExist
[Sat Jan  5 01:51:31 2013] [WARN] (fparser)4987# Unknown event (Gamin): FAMEndExist
[Sat Jan  5 01:51:32 2013] [DEBUG] (worker #2)4993# Queue "/workers_queue":
        - stores at most 10 messages
        - large at most 8192 bytes each
        - currently holds 0 messages

```

That's it, cached started.

## Helper scripts
We need to clean our tables periodically, there's a crontab in root `cached_scheduler`, place it near your database.
