/*
 * SimplePgSQL.c - Lightweight PostgreSQL connector for Arduino
 * Copyright (C) Bohdan R. Rau 2016 <ethanak@polip.com>
 *
 * SimplePgSQL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SimplePgSQL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SimplePgSQL.  If not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */

 // 238 469 / 34004
#include "SimplePgSQL.h"

#ifdef PG_USE_MD5
static void
bytesToHex(const uint8_t b[16], char *s)
{
	int q, w;
    static PROGMEM const char hex[] = "0123456789abcdef";

	for (q = 0, w = 0; q < 16; q++)
	{
		s[w++] = pgm_read_byte(&hex[(b[q] >> 4) & 0x0F]);
		s[w++] = pgm_read_byte(&hex[b[q] & 0x0F]);
	}
	s[w] = '\0';
}

#ifdef ESP8266
#include <md5.h>
static void pg_md5_encrypt(const char *password, char *salt, int salt_len, char *outbuf)
{
    md5_context_t context;
	uint8_t sum[16];
    *outbuf++ = 'm';
    *outbuf++ = 'd';
    *outbuf++ = '5';
    MD5Init(&context);
    MD5Update(&context, (uint8_t *)password, strlen(password));
    MD5Update(&context, (uint8_t *)salt, salt_len);
    MD5Final(sum, &context);
	bytesToHex(sum, outbuf);
}
#else
#include <MD5.h>
static void pg_md5_encrypt(const char *password, char *salt, int salt_len, char *outbuf)
{
	MD5_CTX context;
	uint8_t sum[16];
    *outbuf++ = 'm';
    *outbuf++ = 'd';
    *outbuf++ = '5';

    MD5::MD5Init(&context);
    MD5::MD5Update(&context, (uint8_t *)password, strlen(password));
    MD5::MD5Update(&context, (uint8_t *)salt, salt_len);
    MD5::MD5Final(sum, &context);
	bytesToHex(sum, outbuf);
}
#endif
#endif

#define MD5_PASSWD_LEN	35
#define AUTH_REQ_OK			0	/* User is authenticated  */
#define AUTH_REQ_PASSWORD	3	/* Password */
#define AUTH_REQ_MD5		5	/* md5 password */

static PROGMEM const char EM_OOM [] = "Out of memory";
static PROGMEM const char EM_READ [] = "Backend read error";
static PROGMEM const char EM_WRITE [] = "Backend write error";
static PROGMEM const char EM_CONN [] = "Cannot connect to server";
static PROGMEM const char EM_SYNC [] = "Backend out of sync";
static PROGMEM const char EM_INTR [] = "Internal error";
static PROGMEM const char EM_UAUTH [] = "Unsupported auth method";
static PROGMEM const char EM_BIN [] = "Binary format not supported";
static PROGMEM const char EM_EXEC [] = "Previous execution not finished";
static PROGMEM const char EM_PASSWD [] = "Password required";
static PROGMEM const char EM_EMPTY [] = "Query is empty";
static PROGMEM const char EM_FORMAT [] = "Illegal formatting character";

PGconnection::PGconnection(Client *c,
        int flags,
        int memory,
        char *foreignBuffer)
{
    conn_status = CONNECTION_NEEDED;
    client = c;
    Buffer = foreignBuffer;
    _passwd = NULL;
    _flags = flags & ~PG_FLAG_STATIC_BUFFER;

    if (memory <= 0) bufSize = PG_BUFFER_SIZE;
    else bufSize = memory;
    if (foreignBuffer) {
        _flags |= PG_FLAG_STATIC_BUFFER;
    }
}

int PGconnection::setDbLogin(IPAddress server,
    const char *user,
    const char *passwd,
    const char *db,
    const char *charset,
    int port)
{

    char	   *startpacket;
    int			packetlen;
    int len;

    close();
    if (!db) db = user;
    len = strlen(user) + 1;
    if (passwd) {
        len += strlen(passwd) + 1;
    }
    _user = (char *)malloc(len);
    strcpy(_user, user);
    if (passwd) {
        _passwd = _user + strlen(user) + 1;
        strcpy(_passwd, passwd);
    }
    else {
        _passwd = NULL;
    }
    if (!Buffer) Buffer = (char *) malloc(bufSize);
    byte connected = client -> connect(server, port);
    if (!connected) {
        setMsg_P(EM_CONN, PG_RSTAT_HAVE_ERROR);
        return conn_status = CONNECTION_BAD;
    }
    packetlen = build_startup_packet(NULL, db, charset);
    if (packetlen > bufSize - 10) {
        setMsg_P(EM_OOM, PG_RSTAT_HAVE_ERROR);
        conn_status = CONNECTION_BAD;
        return conn_status;
    }
    startpacket=Buffer + (bufSize - (packetlen + 1));
    build_startup_packet(startpacket, db, charset);
    if (pqPacketSend(0, startpacket, packetlen) < 0) {
        setMsg_P(EM_WRITE, PG_RSTAT_HAVE_ERROR);
        return conn_status = CONNECTION_BAD;
    }
    attempts = 0;
    return conn_status = CONNECTION_AWAITING_RESPONSE;
}

void PGconnection::close(void)
{
    if (client->connected()) {
        pqPacketSend('X', NULL, 0);
        client->stop();
    }
    if (Buffer && !(_flags & PG_FLAG_STATIC_BUFFER)) {
        free(Buffer);
        Buffer = NULL;
    }
    if (_user) {
        free(_user);
        _user = _passwd = NULL;
    }
    conn_status = CONNECTION_NEEDED;
}

int PGconnection::status(void)
{
    char bereq;
    char rc;
    int32_t msgLen;
    int32_t areq;
    char * pwd = _passwd;
    char salt[4];

    switch(conn_status) {
        case CONNECTION_NEEDED:
        case CONNECTION_OK:
        case CONNECTION_BAD:

        return conn_status;

        case CONNECTION_AWAITING_RESPONSE:
        if (!client->available()) return conn_status;
        if (attempts++ >= 2) {
            setMsg_P(EM_SYNC, PG_RSTAT_HAVE_ERROR);
            return conn_status = CONNECTION_BAD;
        }
        if (pqGetc(&bereq)) {
            goto read_error;
        }
        if (bereq == 'E') {
            pqGetInt4(&msgLen);
            pqGetNotice(PG_RSTAT_HAVE_ERROR);
            return conn_status = CONNECTION_BAD;
        }
        if (bereq != 'R') {
            setMsg_P(EM_SYNC, PG_RSTAT_HAVE_ERROR);
            return conn_status = CONNECTION_BAD;
        }
        if (pqGetInt4(&msgLen)) {
            goto read_error;
        }
        if (pqGetInt4(&areq)) {
            goto read_error;
        }
        if (areq == AUTH_REQ_OK) {
            if (_user) {
                free(_user);
                _user = _passwd=NULL;
            }
            result_status = PG_RSTAT_READY;
            return conn_status = CONNECTION_AUTH_OK;
        }
        if (
#ifdef PG_USE_MD5
                areq != AUTH_REQ_MD5 &&
#endif
                areq !=  AUTH_REQ_PASSWORD) {
            setMsg_P(EM_UAUTH, PG_RSTAT_HAVE_ERROR);
            return conn_status = CONNECTION_BAD;
        }
        if (!_passwd || !*_passwd) {
            setMsg_P(EM_PASSWD, PG_RSTAT_HAVE_ERROR);
            return conn_status = CONNECTION_BAD;
        }
        pwd = _passwd;
#ifdef PG_USE_MD5
        if (areq == AUTH_REQ_MD5) {
            if (pqGetnchar(salt, 4)) goto read_error;
            if (bufSize < 3 * MD5_PASSWD_LEN + 10) {
                setMsg_P(EM_OOM, PG_RSTAT_HAVE_ERROR);
                return conn_status = CONNECTION_BAD;
            }
            char *crypt_pwd = Buffer + (bufSize - (2 * (MD5_PASSWD_LEN + 1)));
            char *crypt_pwd2 = crypt_pwd + MD5_PASSWD_LEN + 1;
            pg_md5_encrypt(pwd, _user, strlen(_user), crypt_pwd2);
            pg_md5_encrypt(crypt_pwd2 + 3, salt,4, crypt_pwd);
            pwd = crypt_pwd;
        }
#endif
        rc=pqPacketSend('p', pwd, strlen(pwd) + 1);
        if (rc) {
            goto write_error;
        }
        return conn_status;

        case CONNECTION_AUTH_OK:
        for (;;) {
            if (!client -> available()) return conn_status;
            if (pqGetc(&bereq)) goto read_error;
            if (pqGetInt4(&msgLen)) goto read_error;
            msgLen -= 4;
            if (bereq == 'A' || bereq == 'N' || bereq == 'S' || bereq == 'K') {
                if (pqSkipnchar(msgLen))  goto read_error;
                continue;
            }
            if (bereq == 'E') {
                pqGetNotice(PG_RSTAT_HAVE_ERROR);
                return conn_status = CONNECTION_BAD;
            }

/*            if (bereq == 'K') {
                if (pqGetInt4(&be_pid)) goto read_error;
                if (pqGetInt4(&be_key)) goto read_error;
                continue;
            }
*/
            if (bereq == 'Z') {
                pqSkipnchar(msgLen);
                return conn_status = CONNECTION_OK;
            }
            return conn_status = CONNECTION_BAD;
        }

        default:
        setMsg_P(EM_INTR, PG_RSTAT_HAVE_ERROR);
        return conn_status = CONNECTION_BAD;
    }
read_error:
    setMsg_P(EM_READ, PG_RSTAT_HAVE_ERROR);
    return conn_status = CONNECTION_BAD;
write_error:
    setMsg_P(EM_WRITE, PG_RSTAT_HAVE_ERROR);
    return conn_status = CONNECTION_BAD;
}

int PGconnection::execute(const char *query, int progmem)
{
    if (!(result_status & PG_RSTAT_READY)) {
        setMsg_P(EM_EXEC, PG_RSTAT_HAVE_ERROR);
        return -1;
    }
    int len = progmem ? strlen_P(query) :strlen(query);
    if (pqPacketSend('Q', query, len+1, progmem)) {
        setMsg_P(EM_WRITE, PG_RSTAT_HAVE_ERROR);
        conn_status = CONNECTION_BAD;
        return -1;
    }
    result_status = PG_RSTAT_COMMAND_SENT;
    return 0;
}
int PGconnection::escapeName(const char *inbuf, char *outbuf)
{
    const char *c;
    int l = 2;
    for (c=inbuf; *c; c++) {
        l++;
        if (*c == '\\' || *c == '"') l++;
    }
    if (outbuf) {
        *outbuf++='"';
        for (c=inbuf; *c; c++) {
            *outbuf++ = *c;
            if (*c == '\\' || *c == '"') *outbuf++ = *c;
        }
        *outbuf++='"';
    }
    return l;
}

int PGconnection::escapeString(const char *inbuf, char *outbuf)
{
    const char *c;
    int e = 0, l;
    for (c=inbuf; *c; c++) {
        if (*c == '\\' || *c == '\'') e++;
    }
    l = e + (c - inbuf) + (e ? 4 : 2);
    if (outbuf) {
        if (e) {
            *outbuf++=' ';
            *outbuf++='E';
        }
        *outbuf++='\'';
        for (c=inbuf; *c; c++) {
            *outbuf++ = *c;
            if (*c == '\\' || *c == '\'') *outbuf++ = *c;
        }
        *outbuf++='\'';
    }
    return l;
}

char * PGconnection::getValue(int nr)
{
    int i;
    if (_null & (1<<nr)) return NULL;
    char *c=Buffer;
    if (nr < 0 || nr >= _nfields) return NULL;
    for (i=0; i < nr; i++) {
        if (_null & (1 <<i)) continue;
        c += strlen(c) + 1;
    }
    return c;
}

char *PGconnection::getColumn(int n)
{
    char *c;int i;
    if (!(result_status & PG_RSTAT_HAVE_COLUMNS)) return NULL;
    if (n < 0 || n >= _nfields) return NULL;
    for (c = Buffer, i = 0; i<n; i++) {
        c += strlen(c) + 1;
    }
    return c;
}

char *PGconnection::getMessage(void)
{
    if (!(result_status & PG_RSTAT_HAVE_MESSAGE)) return NULL;
    return Buffer;
}

int PGconnection::getData(void)
{
    char id;
    int32_t msgLen;
    int rc;
    char *c;
    if (!client->available()) return 0;
    if (pqGetc(&id)) goto read_error;
    if (pqGetInt4(&msgLen)) goto read_error;
    //Serial.printf("ID=%c\n", id);
    msgLen -= 4;
    switch(id) {
        case 'T':
        if ((rc=pqGetRowDescriptions())) {
            if (rc == -2) setMsg_P(EM_OOM, PG_RSTAT_HAVE_ERROR);
            else if (rc == -3) setMsg_P(EM_BIN, PG_RSTAT_HAVE_ERROR);
            goto read_error;
        }
        if (_flags & PG_FLAG_IGNORE_COLUMNS) {
            result_status &= ~PG_RSTAT_HAVE_MASK;
            return 0;
        }
        return result_status = (result_status & ~PG_RSTAT_HAVE_MASK) | PG_RSTAT_HAVE_COLUMNS;

        case 'E':
        if (pqGetNotice(PG_RSTAT_HAVE_ERROR)) goto read_error;
        return result_status;

        case 'N':
        if (_flags & PG_FLAG_IGNORE_NOTICES) {
            if (pqSkipnchar(msgLen)) goto read_error;
            return 0;
        }
        if(pqGetNotice(PG_RSTAT_HAVE_NOTICE)) goto read_error;
        return result_status = (result_status & ~PG_RSTAT_HAVE_MASK) | PG_RSTAT_HAVE_NOTICE;

        case 'A':
        if (_flags & PG_FLAG_IGNORE_NOTICES) {
            if (pqSkipnchar(msgLen)) goto read_error;
            return 0;
        }
        if (pqGetNotify(msgLen)) goto read_error;
        return result_status = (result_status & ~PG_RSTAT_HAVE_MASK) | PG_RSTAT_HAVE_NOTICE;

        case 'Z':
        if (pqSkipnchar(msgLen)) goto read_error;
        result_status = (result_status & PG_RSTAT_HAVE_SUMMARY) | PG_RSTAT_READY;
        return PG_RSTAT_READY;

        case 'S': // parameters setting ignored
        case 'K': // should not be here?
        if (pqSkipnchar(msgLen)) goto read_error;
        return 0;

        case 'C': // summary
        if (msgLen > bufSize - 1) goto oom;
        if (pqGetnchar(Buffer, msgLen)) goto read_error;
        Buffer[msgLen] = 0;
        _ntuples = 0;
        result_status = (result_status & ~PG_RSTAT_HAVE_MASK) | PG_RSTAT_HAVE_SUMMARY;
        for (c = Buffer; *c && !isdigit(*c); c++);
        if (!*c) return result_status;
        if (strncmp(Buffer,"SELECT ",7)) {
            for (; *c && isdigit(*c); c++);
            for (; *c && !isdigit(*c); c++);
        }
        if (*c) _ntuples = strtol(c, NULL, 10);
        return result_status;

        case 'D':
        if ((rc=pqGetRow())) {
            if (rc == -2) setMsg_P(EM_OOM, PG_RSTAT_HAVE_ERROR);
            else if (rc == -3) setMsg_P(EM_SYNC, PG_RSTAT_HAVE_ERROR);
            goto read_error;
        }
        if (_flags & PG_FLAG_IGNORE_COLUMNS) {
            result_status &= ~PG_RSTAT_HAVE_MASK;
            return 0;
        }
        return result_status = (result_status & ~PG_RSTAT_HAVE_MASK) | PG_RSTAT_HAVE_ROW;

        case 'I':
        if (pqSkipnchar(msgLen)) goto read_error;
        setMsg_P(EM_EMPTY, PG_RSTAT_HAVE_ERROR);
        return result_status;

        default:
        setMsg_P(EM_SYNC, PG_RSTAT_HAVE_ERROR);
        conn_status = CONNECTION_BAD;
        return -1;
    }

oom:
    setMsg_P(EM_OOM, PG_RSTAT_HAVE_ERROR);

read_error:
    if (!(result_status & PG_RSTAT_HAVE_ERROR)) {
        setMsg_P(EM_READ, PG_RSTAT_HAVE_ERROR);
    }
    conn_status = CONNECTION_BAD;
    return -1;
}

int PGconnection::executeFormat(int progmem, const char *format, ...)
{
    int32_t msgLen;
    va_list va;
    va_start(va, format);
    msgLen = writeFormattedQuery(0, progmem, format, va);
    va_end(va);
    if (msgLen < 0) return -1;
    va_start(va, format);
    msgLen = writeFormattedQuery(msgLen, progmem, format, va);
    va_end(va);
    if (msgLen) {
        return -1;
    }
    result_status = PG_RSTAT_COMMAND_SENT;
    return 0;
}

#ifdef ESP8266

// there is no strchr_P in ESP8266 ROM :(

static const char *strchr_P(const char *str, char c)
{
    char z;
    for (;;) {
        z = pgm_read_byte(str);
        if (!z) return NULL;
        if (z == c) return str;
        str++;
    }
}

#endif


int PGconnection::build_startup_packet(
    char *packet,
    const char *db,
    const char *charset)
{
    int packet_len = 4;
    if (packet) {
        memcpy(packet,"\0\003\0\0", 4);
    }
#define ADD_STARTUP_OPTION(optname, optval) \
	do { \
		if (packet) \
			strcpy_P(packet + packet_len, (char *)optname); \
		packet_len += strlen_P((char *)optname) + 1; \
		if (packet) \
			strcpy(packet + packet_len, (char *)optval); \
		packet_len += strlen((char *)optval) + 1; \
	} while(0)

#define ADD_STARTUP_OPTION_P(optname, optval) \
	do { \
		if (packet) \
			strcpy_P(packet + packet_len, (char *)optname); \
		packet_len += strlen_P((char *)optname) + 1; \
		if (packet) \
			strcpy_P(packet + packet_len, (char *)optval); \
		packet_len += strlen_P((char *)optval) + 1; \
	} while(0)

	if (_user && _user[0])
		ADD_STARTUP_OPTION(PSTR("user"), _user);
	if (db && db[0])
		ADD_STARTUP_OPTION(PSTR("database"), db);
	if (charset && charset[0])
		ADD_STARTUP_OPTION(PSTR("client_encoding"), charset);
    ADD_STARTUP_OPTION_P(PSTR("application_name"), PSTR("arduino"));
#undef ADD_STARTUP_OPTION
	if (packet)
		packet[packet_len] = '\0';
	packet_len++;

	return packet_len;
}

int PGconnection::pqPacketSend(char pack_type, const char *buf, int buf_len, int progmem)
{
    char *start = Buffer;
    int l = bufSize - 4;
    int n;
    if (pack_type) {
        *start++ = pack_type;
        l--;
    }
    *start++ = ((buf_len + 4) >> 24) & 0xff;
    *start++ = ((buf_len + 4) >> 16) & 0xff;
    *start++ = ((buf_len + 4) >> 8) & 0xff;
    *start++ = (buf_len + 4) & 0xff;
    if (progmem) {
        while (buf_len > 0) {
            while (buf_len > 0 && l > 0) {
                *start++ = pgm_read_byte(buf++);
                buf_len--;
            }
            n = client->write((const uint8_t *)Buffer, start - Buffer);
            if (n != start - Buffer) return -1;
            start = Buffer;
            l = bufSize;
        }
    }
    else {
        if (buf) {
            if (buf_len <= l) {
                memcpy(start, buf, buf_len);
                start += buf_len;
                buf_len = 0;
            }
            else {
                memcpy(start, buf, l);
                start += l;
                buf_len -= l;
                buf += l;
            }
        }
        n = client->write((const uint8_t *)Buffer, start - Buffer);
        if (n != start - Buffer) return -1;
        if (buf && buf_len) {
            n = client->write((const uint8_t *)buf, buf_len);
            if (n != buf_len) return -1;
        }
    }
    return 0;
}

int PGconnection::pqGetc(char *buf)
{
    int i;
    for (i=0; !client->available() && i < 10; i++) {
        delay (i * 10 + 10);
    }
    if (!client->available()) {
        return -1;
    }
    *buf = client->read();
    return 0;
}

int PGconnection::pqGetInt4(int32_t *result)
{
	uint32_t tmp4 = 0;
    byte tmp,i;
    for (i = 0; i < 4; i++) {
        if (pqGetc((char *)&tmp)) return -1;
        tmp4 = (tmp4 << 8) | tmp;
    }
    *result = tmp4;
    return 0;
}

int PGconnection::pqGetInt2(int16_t *result)
{
	uint16_t tmp2 = 0;
    byte tmp,i;
    for (i = 0; i < 2; i++) {
        if (pqGetc((char *)&tmp)) return -1;
        tmp2 = (tmp2 << 8) | tmp;
    }
    *result = tmp2;
    return 0;
}

int PGconnection::pqGetnchar(char *s, int len)
{
    while (len-- > 0) {
        if (pqGetc(s++)) return -1;
    }
    return 0;
}

int PGconnection::pqGets(char *s, int maxlen)
{
    int len;
    char z;
    for (len = 0;len < maxlen; len++) {
        if (pqGetc(&z)) return -1;
        if (s) *s++ = z;
        if (!z) return len+1;
    }
    return - (len + 1);
}

int PGconnection::pqSkipnchar(int len)
{
    char dummy;
    while (len-- > 0) {
        if (pqGetc(&dummy)) return -1;
    }
    return 0;
}

int PGconnection::pqGetRow(void)
{
    int i;
    int bufpos = 0;
    int32_t len;
    int16_t cols;

    _null = 0;
    if (pqGetInt2(&cols)) return -1;
    if (cols != _nfields) {
        return -3;
    }
    for (i=0; i < _nfields; i++) {
        if (pqGetInt4(&len)) return -1;
        if (len < 0) {
            _null |= 1<<i;
            continue;
        }
        if (bufpos + len + 1 > bufSize) {
            return -2;
        }
        if (pqGetnchar(Buffer + bufpos, len)) return -1;
        bufpos += len;
        Buffer[bufpos++]=0;
    }
    return 0;
}


int PGconnection::pqGetRowDescriptions(void)
{
    int i;
    int16_t format;
    int rc;
    int bufpos;
    if (pqGetInt2(&_nfields)) return -1;
    if (_nfields > PG_MAX_FIELDS) return -2; // implementation limit
    _formats = 0;
    bufpos = 0;
    for (i = 0;i < _nfields; i++) {
        if (!(_flags & PG_FLAG_IGNORE_COLUMNS)) {
            if (bufpos >= bufSize - 1) return -2;
            rc = pqGets(Buffer + bufpos, bufSize - bufpos);
            if (rc < 0) return -1;
            bufpos += rc;
        }
        else {
            if (pqGets(NULL, 8192) < 0) {
                return -1;
            }
        }
        if (pqSkipnchar(16)) return -1;
        if (pqGetInt2(&format)) return -1;
        format = format ? 1 : 0;
        _formats |= format << i;
    }
    if (_formats) return -3;
    return 0;
}

void PGconnection::setMsg(const char *s, int type)
{
    strcpy(Buffer, s);
    result_status = (result_status & ~PG_RSTAT_HAVE_MASK) | type;
}

void PGconnection::setMsg_P(const char *s, int type)
{
    strcpy_P(Buffer, s);
    result_status = (result_status & ~PG_RSTAT_HAVE_MASK) | type;
}

int PGconnection::pqGetNotice(int type)
{
    int bufpos = 0;
    char id;
    int rc;
    for (;;) {
        if (pqGetc(&id)) goto read_error;
        if (!id) break;
        if (id == 'S' || id == 'M') {
            if (bufpos && bufpos < bufSize - 1) Buffer[bufpos++]=':';
            rc = pqGets(Buffer + bufpos, bufSize - bufpos);
            if (rc < 0) goto read_error;
            bufpos += rc -1;
        }
        else {
            rc = pqGets(NULL, 8192);
            if (rc < 0) goto read_error;
        }
    }
    Buffer[bufpos] = 0;
    result_status = (result_status & ~PG_RSTAT_HAVE_MASK) | type;
    return 0;

read_error:
    if (!bufpos) setMsg_P(EM_READ, PG_RSTAT_HAVE_ERROR);
    return -1;
}

int PGconnection::pqGetNotify(int32_t msgLen)
{
    int32_t pid;
    int bufpos, i;
    if (pqGetInt4(&pid)) return -1;
    msgLen -= 4;
    bufpos = sprintf(Buffer,"%d:",pid);
    if (msgLen > bufSize - (bufpos + 1)) {
        if (pqGetnchar(Buffer+bufpos, bufSize - (bufpos + 1)))
            return -1;
        msgLen -= bufSize - (bufpos + 1);
        if (pqSkipnchar(msgLen)) return -1;
        Buffer[msgLen = bufSize - 1] = 0;

    }
    else {
        if (pqGetnchar(Buffer+ bufpos, msgLen)) return -1;
        Buffer[bufpos + msgLen] = 0;
        msgLen += bufpos;
    }
    for (i=0; i<msgLen; i++) if (!Buffer[i]) Buffer[i] = ':';
    return 0;
}


int PGconnection::writeMsgPart_P(const char *s, int len, int fine)
{
    while (len > 0) {
        if (bufPos >= bufSize) {
            if (client->write((uint8_t *)Buffer, bufPos) != (size_t)bufPos) {
                return -1;
            }
            bufPos = 0;
        }
        Buffer[bufPos++] = pgm_read_byte(s++);
        len--;
    }
    if (bufPos && fine) {
        if (client->write((uint8_t *)Buffer, bufPos) != (size_t)bufPos) {
            return -1;
        }
        bufPos = 0;
    }
    return 0;
}

int PGconnection::writeMsgPart(const char *s, int len, int fine)
{
    while (len > 0) {
        int n = len;
        if (n > bufSize - bufPos) n = bufSize - bufPos;
        memcpy(Buffer + bufPos, s, n);
        bufPos += n;
        s += n;
        len -= n;
        if (bufPos >= bufSize) {
            if (client->write((uint8_t *)Buffer, bufPos) != (size_t)bufPos) {
                return -1;
            }
            bufPos = 0;
        }
    }
    if (bufPos && fine) {
        if (client->write((uint8_t *)Buffer, bufPos) != (size_t)bufPos) {
            return -1;
        }
        bufPos = 0;
    }

    return 0;
}

int32_t PGconnection::writeFormattedQuery(int32_t length, int progmem, const char *format, va_list va)
{
    int32_t msgLen = 0;
    const char *percent;
    int blen, rc;
    char buf[32], znak;
    if (length) {
        length += 4;
        bufPos = 0;
        Buffer[bufPos++] = 'Q';
        Buffer[bufPos++] = (length >> 24) & 0xff;
        Buffer[bufPos++] = (length >> 16) & 0xff;
        Buffer[bufPos++] = (length >> 8) & 0xff;
        Buffer[bufPos++] = (length) & 0xff;
    }
    for (;;) {
        if (progmem) {
            percent = strchr_P(format, '%');
        }
        else {
            percent = strchr(format, '%');
        }
        if (!percent) break;
        if (progmem) {
            znak = pgm_read_byte(percent+1);
        }
        else {
            znak = percent[1];
        }
        if (!length) {
            msgLen += (percent - format);
        }
        else {
            if (progmem) {
                rc = writeMsgPart_P(format, percent - format, false);
            }
            else {
                rc = writeMsgPart(format, percent - format, false);
            }
            if (rc) goto write_error;
        }
        format = percent + 2;
        if (znak == 's' || znak == 'n') {
            char *str = va_arg(va, char *);
            blen = (znak == 's') ? escapeString(str, NULL) : escapeName(str, NULL);
            if (!length) {
                msgLen += blen;
            }
            else {
                if (bufPos + blen > bufSize) {
                    rc = writeMsgPart(NULL, 0, true);
                    if (rc) goto write_error;
                }
            }
            if (znak == 's') {
                escapeString(str, Buffer + bufPos);
            }
            else {
                escapeName(str, Buffer + bufPos);
            }
            bufPos += blen;
            continue;
        }
        if (znak == 'l' || znak == 'd') {
            if (znak == 'l') {
                long n = va_arg(va, long);
                blen = snprintf(buf, 32, "'%ld'", n);
            }
            else {
                int n = va_arg(va, int);
                blen = snprintf(buf, 32, "'%d'", n);
            }
            if (length) {
                rc = writeMsgPart(buf, blen, false);
                if (rc) goto write_error;
            }
            else {
                msgLen += blen;
            }
        }
        setMsg_P(EM_FORMAT, PG_RSTAT_HAVE_ERROR);
        return -1;
    }
    if (progmem) {
        blen = strlen_P(format);
    }
    else {
        blen = strlen(format);
    }
    if (length) {
        if (progmem) {
            rc = writeMsgPart_P(format, blen, false);
        }
        else {
            rc = writeMsgPart(format, blen, false);
        }
        if (!rc) {
            rc = writeMsgPart("\0",1,true);
        }
        if (rc) goto write_error;
    }
    else {
        msgLen += blen + 1;
    }
    return msgLen;
write_error:
    setMsg_P(EM_WRITE, PG_RSTAT_HAVE_ERROR);
    conn_status = CONNECTION_BAD;
    return -1;
}
