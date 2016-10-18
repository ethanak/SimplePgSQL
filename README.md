# SimplePgSQL
Simple PostgreSQL connector for Arduino and ESP8266

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


         * returns connection status.
         * passwd may be null in case of 'trust' authorization.
         * only 'trust', 'password' and 'md5' (if compiled in)
         * authorization modes are implemented.
         * ssl mode is not implemented.
         * database name defaults to user name         *
         */

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
```
       /*
         * performs authorization tasks if needed
         * returns current connection status
         * must be called periodically until OK, BAD or NEEDED
         */
        /*
         * sends termination command if possible
         * closes client connection and frees internal buffer
         */
        void close(void);
        /*
         * sends query to backend
         * returns negative value on error
         * or zero on success
         */
        int execute(const char *query, int progmem = 0);

        /* should be called periodically in idle state
         * if notifications are enabled
         * returns:
         * - negative value on error
         * - zero if no interesting data arrived
         * - current data status if some data arrived
         */
        int getData(void);
        /*
         * returns pointer to n-th column name in internal buffer
         * if available or null if column number out of range
         * will be invalidated on next getData call
         */
        char *getColumn(int n);
        /*
         * returns pointer to n-th column value in internal buffer
         * if available or null if column number out of range
         * or value is NULL
         * will be invalidated on next getData call
         */
        char *getValue(int);
        /*
         * returns pointer to message (error or notice)
         * if available or NULL
         * will be invalidated on next getData call
         */
        char *getMessage(void);
        int dataStatus(void) {
            return result_status;
        };
        int nfields(void) {
            return _nfields;
        };
        int ntuples(void) {
            return _ntuples;
        };
        /*
         * returns length of escaped string
         * single quotes and E prefix (if needed)
         * will be added.
         */
        int escapeString(const char *inbuf, char *outbuf);
        /*
         * returns length of escaped string
         * double quotes will be added.
         */
        int escapeName(const char *inbuf, char *outbuf);
        /*
         * sends formatted query to backend
         * returns negative value on error
         * or zero on success
         * Formatting sequences:
         * %s - string literal (will be escaped with escapeString)
         * %n - name (will be escaped with escapeName)
         * %d - int (single quotes will be added)
         * %l - long int (single quotes will be added)
         * %% - % character
         */
        int executeFormat(int progmem, const char *format, ...);
