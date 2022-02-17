# SimplePgSQL

Simple PostgreSQL connector for Arduino, ESP32 and ESP8266.

Only simple queries are implemented. `COPY` is not implemented due to code size limit,
but probably will be for ESP32 only. Large objects are not implemented as obsolete
and never be.

Available authorization method are `trust`, `password` and `md5`.
Due to code size limit, `md5` method may be disabled in compilation time,
decreasing code size for Arduino by some kilobytes.

All methods are asynchronous, but sometimes may block for a while in case of poor network connection.

As column names and notifications are rarely needed in microcontrollers applications, may be disabled.
In this case only number of fields will be fetched from row description packet, and notification rows will be simply skipped.

All methods uses single internal buffer, allocated at `setDbLogin` and freed on `close`.
It's possible to provide external (statically allocated) buffer, which may be reused in rest of application.

Parameter `progmem` has no meaning for ESP32.

### Class and Methods
  * [PGconnection](#pgconnection)
  * [setDbLogin](#setdblogin)
  * [status](#status)
  * [close](#close)
  * [execute](#execute)
  * [getData](#getdata)
  * [getColumn](#getcolumn)
  * [getValue](#getvalue)
  * [getMessage](#getmessage)
  * [dataStatus](#datastatus)
  * [nfields](#nfields)
  * [ntuples](#ntuples)
  * [escapeString](#escapestgring)
  * [escapeName](#escapename);
  * [executeFormat](#executeformat);


### PGconnection
```cpp
PGconnection(Client *c, int flags = 0, int memory = 0, char *foreignBuffer = NULL);
```
Class constructor.
#### Parameters:
  * `Client` - Client instance (WiFi or Ethernet)
  * `flags` - some flags:
      - `PG_FLAG_IGNORE_NOTICES` - ignore notices and notifications
      - `PG_FLAG_IGNORE_COLUMNS` - ignore column names
  * `memory` - internal buffer size. Defaults to PG_BUFFER_SIZE
  * `foreignBuffer` - static buffer address

### setDbLogin
```cpp
int setDbLogin(IPAddress server,
            const char *user,
            const char *passwd = NULL,
            const char *db = NULL,
            const char *charset = NULL,
            int port = 5432);
```
Initialize connection.
#### Parameters:
  * `server` - IP address of backend server
  * `user` - database user name
  * `passwd` - database user password
  * `db` - database name (defaults to user name)
  * `charset` - client encoding
  * `port` - server port

#### Returns:
  connection status (see below)

### status
```cpp
int status(void);
```
Check current connection status and perform authorization action if needed.
Must be called after `setDbLogin` until `CONNECTION_OK`, `CONNECTION_BAD` or `CONNECTION_NEEDED`.

#### Returns

Current connection status. May be one of:

  * `CONNECTION_NEEDED` - no connection yet, call `setDbLogin`.
  * `CONNECTION_OK` - ready for queries.
  * `CONNECTION_BAD` - connection can't be realized or was abandoned. Call `close()`.
  * `CONNECTION_AWAITING_RESPONSE` - Waiting for a response from the postmaster. Call `status()` again.
  * `CONNECTION_AUTH_OK` - Received authentication; waiting for backend startup. Call `status()` again.

### close
```cpp
void close(void);
```
Send termination command if needed and close connection. Free internal buffers.

### execute
```cpp
execute(const char *query, int progmem = 0);
```
Send query to backend. Invalidates data in internal buffer.
#### Parameters:
  * `query` - PostgreSQL query
  * `progmem` - if not zero, query is in Flash memory

#### Returns

Negative value on error or zero on success.
In case of error you must check connection status - some errors invalidates connection.

### getData
```cpp
int getData(void);
```

Retrieve any response from backend.
Must be called after `execute()` until `PG_RSTAT_READY`.
May be called even in READY state and fetch possible notifications.
Each call invalidates data in internal buffer, so values returned by
`getColumn()`, `getValue()` and `getMessage()` will be valid only until next `getData()` or `execute()` call.

#### Returns:
  * Negative value on error
  * Zero if no interesting data arrived
  * Positive value if some data was fetched (see below)

### dataStatus
```cpp
int dataStatus(void);
```
Get current data status.

#### Returns
Combination of bits:
  - `PG_RSTAT_READY` - ready for next query
  - `PG_RSTAT_COMMAND_SENT` - command was sent to backend
  - `PG_RSTAT_HAVE_COLUMNS` - column names in buffer
  - `PG_RSTAT_HAVE_ROW` - row data in buffer
  - `PG_RSTAT_HAVE_SUMMARY` - execution finished, number of affected rows available
  - `PG_RSTAT_HAVE_ERROR` - error message in buffer
  - `PG_RSTAT_HAVE_NOTICE` - notice/notification in buffer

### getColumn
```cpp
char *getColumn(int n);
```
Get n-th column name if available

#### Returns
  * Pointer to n-th column name in internal buffer
  * NULL if not in `PG_RSTAT_HAVE_COLUMNS` state or n out of range.

### getValue
```cpp
char *getValue(int n);
```
Get n-th row value if available

#### Returns
  * Pointer to n-th column value in internal buffer
  * NULL if value is NULL, n is out of range or not in `PG_RSTAT_HAVE_ROW` state


### getMessage
```cpp
char *getMessage(void);
```
Get current error or notification message

#### Returns
Pointer to message text in internal buffer or NULL if no message.

### nfields
```cpp
int nfields(void);
```
Get column count in result. Valid only after `PG_RSTAT_HAVE_COLUMNS` or first `PG_RSTAT_HAVE_ROW`.

#### Returns

Column count or zero if not known.

### ntuples
```cpp
int ntuples(void);
```
Get number of tuples in result. Valid only after `PG_RSTAT_HAVE_SUMMARY`.

#### Returns
  * Number of returned tuples in case of `SELECT` or `INSERT`/`UPDATE`...`RETURNING`
  * Number of affected rows in case of `INSERT`, `UPDATE` or `DELETE`
  * Zero if not available.

### pgEscapeString
```cpp
int escapeString(const char *inbuf, char *outbuf);
```
Creates escaped version of literal. Single quotes and 'E' marker (if needed) will be added.

#### Parameters:
  * `inbuf` - string to escape
  * `outbuf` - output buffer or NULL if we only count length of result

#### Returns

Length of escaped string

### escapeName
```cpp
int escapeName(const char *inbuf, char *outbuf);
```
Creates escaped version of name. Double quotes will be added.

#### Parameters:
  * `inbuf` - string to escape
  * `outbuf` - output buffer or NULL if we only count length of result

#### Returns

Length of escaped string

### executeFormat
```cpp
int executeFormat(int progmem, const char *format, ...);
```
Send formatted query to backend. Format string may be placed in Flash memory. Formatting sequences are:

  * %s - string literal (will be escaped with escapeString)
  * %n - name (will be escaped with escapeName)
  * %d - int (single quotes will be added)
  * %l - long int (single quotes will be added)
  * %% - % character

Any % character not followed by 's', 'n', 'd', 'l' or '%' causes error.
Query may be long, but results of any formatted value must fit in internal buffer.

#### Parameters:
  * `progmem` - indicates `format` in Flash memory
  * `format` - formatting string

#### Returns

Zero on success or negative value on error.
