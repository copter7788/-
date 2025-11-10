/*
 * ตัวอย่างโปรแกรมเซิร์ฟเวอร์ (vc_server.c)
 * ทำงานแบบ TCP: รอการเชื่อมต่อจากไคลเอนต์ แล้วส่งเนื้อหาไฟล์ตามชื่อที่ไคลเอนต์ร้องขอ
 * โครงสร้างหลัก:
 *  - main(): เตรียมเซิร์ฟเวอร์, accept การเชื่อมต่อ, fork เป็นโปรเซสลูกเพื่อให้บริการ
 *  - setup_vcserver(): สร้างซ็อกเก็ต, bind, และ listen
 *  - send_file(): รับชื่อไฟล์และส่งเนื้อหาไฟล์กลับไปยังไคลเอนต์
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
/* ส่วนติดต่อซ็อกเก็ตพื้นฐาน (socket, bind, listen, accept, send/recv) */
#include <sys/socket.h>
/* โครงสร้างและคงที่ของเครือข่ายแบบอินเทอร์เน็ต (struct sockaddr_in, htons ฯลฯ) */
#include <netinet/in.h>
/* ฟังก์ชันแก้ชื่อโฮสต์ (เช่น gethostbyname) */
#include <netdb.h>
/* ฟังก์ชันระบบยูนิกซ์ทั่วไป (gethostname, fork, close ฯลฯ) */
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#define  MAXHOSTNAME	64
#define  S_TCP_PORT	(u_short)5050  /* พอร์ต TCP ของเซิร์ฟเวอร์ */
#define  MAXFILENAME	255
#define  MAXBUFLEN	512
#define  ERR		0 /* โค้ดตอบกลับ: เกิดข้อผิดพลาด */
#define	 OK		1 /* โค้ดตอบกลับ: สำเร็จ */
int setup_vcserver(struct hostent*, u_short);
void send_file(int);

int main()
{
	int	socd, socd1;
	char	s_hostname[MAXHOSTNAME];
	struct hostent	*s_hostent;
	struct sockaddr_in	c_address;
	int	c_addrlen, cpid;

	/* เตรียมข้อมูลโฮสต์ของเครื่องเซิร์ฟเวอร์และแก้ชื่อเป็นที่อยู่ (hostent) */
	gethostname(s_hostname, sizeof(s_hostname));
	s_hostent = gethostbyname(s_hostname);

	/* สร้างซ็อกเก็ต, bind พอร์ต, และเริ่ม listen */
	socd = setup_vcserver(s_hostent, S_TCP_PORT);

	while(1) {
		/* รอการเชื่อมต่อจากไคลเอนต์ */
		c_addrlen = sizeof(c_address);
		if((socd1 = accept(socd, (struct sockaddr *)&c_address, &c_addrlen)) < 0) {
			perror("accept");
			exit(1);
		}
		/* สร้างโปรเซสลูกเพื่อให้บริการแต่ละการเชื่อมต่อ (concurrent server แบบง่าย) */
		if((cpid = fork()) < 0) { perror("fork");exit(1); }
		else if(cpid == 0) { /* โปรเซสลูก: จัดการคำขอของไคลเอนต์ */
			/* ปิดซ็อกเก็ตที่ใช้รอการเชื่อมต่อในโปรเซสลูก ไม่จำเป็นสำหรับงานส่งไฟล์ */
			close(socd);

			/* รับชื่อไฟล์จากไคลเอนต์และส่งเนื้อหากลับไป */
			send_file(socd1);

			close(socd1);
			exit(0);
		}
		else close(socd1);   /* โปรเซสแม่: ปิดซ็อกเก็ตของลูก แล้ววนรอการเชื่อมต่อใหม่ */
	}
}

int setup_vcserver(struct hostent *hostent, u_short port)
{
	int	socd;
	struct sockaddr_in	s_address;

	/* สร้างซ็อกเก็ต TCP (SOCK_STREAM) */
	if((socd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("socket");exit(1); }
	int opt = 1;
	if (setsockopt(socd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
	    perror("setsockopt(SO_REUSEADDR)");
	    exit(1);
	}

	/* กำหนดค่าโครงสร้างที่อยู่ของเซิร์ฟเวอร์ (family, port, address) */
	bzero((char *)&s_address, sizeof(s_address));
	s_address.sin_family = AF_INET;
	s_address.sin_port = htons(port);
	s_address.sin_addr.s_addr = htonl(INADDR_ANY);

	/* bind ซ็อกเก็ตเข้ากับ (IP, PORT) ของเครื่องนี้ */
	if(bind(socd, (struct sockaddr *)&s_address, sizeof(s_address)) < 0) { perror("bind");exit(1); }

	/* เริ่มฟังการเชื่อมต่อขาเข้า กำหนด backlog = 5 */
	if(listen(socd, 5) < 0) { perror("listen");exit(1); }

	return socd;
}

static int send_one_file(int socd, const char *filename) {
    FILE *fd = fopen(filename, "rb");
    if (!fd) {
        // header: ERR <name>\n
        dprintf(socd, "ERR %s\n", filename);
        return -1;
    }

    // find size
    if (fseek(fd, 0, SEEK_END) != 0) { fclose(fd); dprintf(socd, "ERR %s\n", filename); return -1; }
    long sz = ftell(fd);
    if (sz < 0)          { fclose(fd); dprintf(socd, "ERR %s\n", filename); return -1; }
    rewind(fd);

    // header: OK <name> <size>\n
    dprintf(socd, "OK %s %ld\n", filename, sz);

    char buf[MAXBUFLEN];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fd)) > 0) {
        if (send(socd, buf, n, 0) < 0) { fclose(fd); return -1; }
    }
    fclose(fd);
    return 0;
}

static void handle_get(int socd, char *payload) {
    // payload may be: NULL (legacy single name already in payload) or a list: "fname1 fname2 ..."
    // tokenize by spaces and commas
    const char *delims = " ,\t\r\n";
    char *token = strtok(payload, delims);
    if (!token) return;

    // If it's a single token and no GET: prefix was used previously, keep legacy behaviour
    // Legacy header: 1 byte status (OK/ERR), then stream (no size)
    // We'll switch to new per-file header only when the client used GET:
    // To keep code simple, we always use the new header format when multiple tokens exist.
    int multi = (strtok(NULL, delims) != NULL);

    // Reset tokenization (first token was consumed)
    // Re-tokenize from start
    token = strtok(payload, delims);

    if (!multi) {
        // Single file using new header as well (backward compatibility may require client change)
        send_one_file(socd, token);
        return;
    }

    // Multiple files
    while (token) {
        send_one_file(socd, token);
        token = strtok(NULL, delims);
    }
}

static void handle_put(int socd, const char *filename) {
    // Save incoming bytes to filename until the client closes the connection.
    FILE *fd = fopen(filename, "wb");
    if (!fd) {
        dprintf(socd, "ERR %s\n", filename);
        return;
    }
    char buf[MAXBUFLEN];
    ssize_t n;
    while ((n = recv(socd, buf, sizeof(buf), 0)) > 0) {
        fwrite(buf, 1, (size_t)n, fd);
    }
    fclose(fd);
    dprintf(socd, "OK %s\n", filename);
}

void send_file(int socd)
{
    char buf[MAXBUFLEN];
    int n = recv(socd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    // Trim leading spaces
    char *p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    // Determine command
    if (strncmp(p, "PUT:", 4) == 0) {
        // format: PUT:filename\n  (the rest of the connection is the file body)
        char *fname = p + 4;
        // strip trailing newline
        char *nl = strpbrk(fname, "\r\n");
        if (nl) *nl = '\0';
        printf("Uploading file %s ...\n", fname);
        handle_put(socd, fname);
        return;
    } else if (strncmp(p, "GET:", 4) == 0) {
        // format: GET:fname1 fname2, fname3 ...
        char *list = p + 4;
        // strip trailing newline
        char *nl = strpbrk(list, "\r\n");
        if (nl) *nl = '\0';
        printf("Downloading multiple files: %s\n", list);
        handle_get(socd, list);
        return;
    } else {
        // Legacy path: payload is a single filename (possibly with newline)
        char *fname = p;
        char *nl = strpbrk(fname, "\r\n");
        if (nl) *nl = '\0';
        printf("Sending file (legacy) %s ...\n", fname);

        // For legacy behavior (1-byte ack + raw content), keep compatibility
        FILE *fd = fopen(fname, "r");
        char ack;
        if (fd) {
            ack = OK;
            send(socd, &ack, 1, 0);
            while ((n = (int)fread(buf, 1, sizeof(buf), fd)) > 0) {
                if (send(socd, buf, n, 0) < 0) break;
            }
            fclose(fd);
        } else {
            ack = ERR;
            send(socd, &ack, 1, 0);
        }
        return;
    }
}