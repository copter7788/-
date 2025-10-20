/* โปรแกรมตัวอย่างไคลเอนต์ UDP (dg_client.c) */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h> /* ไลบรารีสำหรับการใช้งานซ็อกเก็ต */
#include <netinet/in.h> /* โครงสร้างข้อมูลสำหรับโปรโตคอลอินเทอร์เน็ต */
#include <netdb.h>		/* สำหรับฟังก์ชัน gethostbyname() */
#include <unistd.h>
#include <errno.h>
#include <string.h>
#define MAXHOSTNAME 64
#define S_UDP_PORT (u_short)5000
#define MAXKEYLEN 128
#define MAXDATALEN 256

int setup_dgclient(struct hostent *, u_short, struct sockaddr_in *, int *);
void remote_dbsearch(int, struct sockaddr_in *, int);

int main()
{
	int socd;
	char s_hostname[MAXHOSTNAME];
	struct hostent *s_hostent;
	struct sockaddr_in s_address;
	int s_addrlen;

	/* รับชื่อโฮสต์ของเซิร์ฟเวอร์จากผู้ใช้ */
	printf("server host name?");
	scanf("%s", s_hostname);
	/* แปลงชื่อโฮสต์เป็นที่อยู่ IP (โครงสร้าง hostent) */
	if ((s_hostent = gethostbyname(s_hostname)) == NULL)
	{
		fprintf(stderr, "server host not found.\n");
		exit(1);
	}

	/* สร้างและกำหนดค่า socket สำหรับ UDP */
	socd = setup_dgclient(s_hostent, S_UDP_PORT, &s_address, &s_addrlen);

	/* เริ่มการค้นหาข้อมูลจากเซิร์ฟเวอร์ */
	remote_dbsearch(socd, &s_address, s_addrlen);

	close(socd);
	exit(0);
}

int setup_dgclient(struct hostent *hostent, u_short port, struct sockaddr_in *s_addressp, int *s_addrlenp)
{
	int socd;
	struct sockaddr_in c_address;

	/* สร้าง socket แบบ SOCK_DGRAM (UDP) */
	if ((socd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}

	/* กำหนดค่าที่อยู่ของเซิร์ฟเวอร์ (Internet domain) */
	bzero((char *)s_addressp, sizeof(*s_addressp));
	s_addressp->sin_family = AF_INET;
	s_addressp->sin_port = htons(port);
	bcopy((char *)hostent->h_addr, (char *)&s_addressp->sin_addr, hostent->h_length);
	*s_addrlenp = sizeof(*s_addressp);

	/* กำหนดค่าที่อยู่ของไคลเอนต์ (Internet domain) */
	bzero((char *)&c_address, sizeof(c_address));
	c_address.sin_family = AF_INET;
	c_address.sin_port = htons(0);				   /* ให้ระบบเลือกพอร์ตอัตโนมัติ */
	c_address.sin_addr.s_addr = htonl(INADDR_ANY); /* ใช้ทุกอินเทอร์เฟซของเครื่อง */

	/* ผูก socket กับที่อยู่ไคลเอนต์ */
	if (bind(socd, (struct sockaddr *)&c_address, sizeof(c_address)) < 0)
	{
		perror("bind");
		exit(1);
	}

	return socd;
}

void remote_dbsearch(int socd, struct sockaddr_in *s_addressp, int s_addrlen) /* ฟังก์ชันส่งคำค้นหาและรับข้อมูลตอบกลับ */
{
	char key[MAXKEYLEN + 1], data[MAXDATALEN + 1];
	int keylen, datalen;

	/* แนะนำวิธีใช้ (ภาษาญี่ปุ่น) */
	printf(
		"―― 使い方 ――\n"
		"  GET:name1,name2,...   : 電話番号を検索\n"
		"  PUT:name,tel-number    : 電話番号を更新\n"
		"  name1,name2,...        : 旧形式の検索\n"
		"  終了は Ctrl+D\n\n"
	);

	/* วนรับคำค้นหาจากผู้ใช้จนกว่าจะกด CTRL+D (EOF) */
	while (1)
	{
		/* อธิบายผู้ใช้ว่าสามารถพิมพ์หลายชื่อได้ โดยคั่นด้วย comma */
		printf("入力 >（終了Ctrl+D）: ");

		/* เมื่อกด CTRL+D -> scanf จะคืนค่า EOF แล้วออกจากลูป */
		if (scanf("%128s", key) == EOF)
		{
			printf("\n終了します。\n");
			break; /* CTRL+D เพื่อออก */
		}

		/* ส่งคำค้นหา (อาจมีหลายชื่อ) ไปยังเซิร์ฟเวอร์ */
		keylen = (int)strlen(key);
		if (sendto(socd, key, keylen, 0, (struct sockaddr *)s_addressp, s_addrlen) != keylen)
		{
			fprintf(stderr, "データグラム送信エラー。\n");
			exit(1);
		}

		/* รอรับข้อมูลตอบกลับจากเซิร์ฟเวอร์ */
		if ((datalen = recvfrom(socd, data, MAXDATALEN, 0, NULL, &s_addrlen)) < 0)
		{
			perror("recvfrom");
			exit(1);
		}

		/* แสดงข้อมูลที่ได้รับ */
		data[datalen] = '\0';
		fputs("結果: ", stdout);
		puts(data);
	}

	/* ออกจากฟังก์ชันเมื่อผู้ใช้กด CTRL+D */
}