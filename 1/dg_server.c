/* ตัวอย่างเซิร์ฟเวอร์ค้นหาข้อมูลแบบไม่ต้องเชื่อมต่อ (dg_server.c) */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h> /* เฮดเดอร์ไฟล์พื้นฐานสำหรับ socket */
#include <netinet/in.h> /* เฮดเดอร์ไฟล์สำหรับโดเมนอินเทอร์เน็ต */
#include <netdb.h>      /* เฮดเดอร์ไฟล์สำหรับใช้ gethostbyname() */
#include <unistd.h>     /* เฮดเดอร์ไฟล์สำหรับใช้ gethostname() */
#include <errno.h>
#include <string.h>
#include <ctype.h>     /* สำหรับ isspace() ใช้ตัดช่องว่างหัว-ท้ายโทเคน */
#define  MAXHOSTNAME	64
#define  S_UDP_PORT	(u_short)5000  /* หมายเลขพอร์ตที่เซิร์ฟเวอร์นี้ใช้ */
#define  MAXKEYLEN	128
#define  MAXDATALEN	256

int setup_dgserver(struct hostent*, u_short);
void db_search(int);

/* ------------------- [ADD] ส่วนเสริม: overlay DB และ helper ------------------- */
typedef struct {
    char name[64];
    char tel[32];
} Entry;

static Entry overlay_db[64];      /* DB ที่แก้ไขได้ (ก็อปจากของเดิม) */
static int overlay_inited = 0;    /* ทำ init ครั้งเดียว */

/* trim ช่องว่างหัว-ท้าย */
static char* trim(char* s){
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char* e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

/* ลบช่องว่างที่ติดกับ comma แต่คง space ภายในชื่อ */
static void compact_commas(char* s){
    char out[MAXKEYLEN+1]; int w=0, n=(int)strlen(s);
    for (int i=0;i<n;i++){
        unsigned char c = (unsigned char)s[i];
        if (isspace(c)){
            int j=i-1; while (j>=0 && isspace((unsigned char)s[j])) j--;
            int k=i+1; while (k<n && isspace((unsigned char)s[k])) k++;
            int prev_is_comma = (j<0) ? 1 : (s[j]==',');
            int next_is_comma = (k>=n) ? 1 : (s[k]==',');
            if (prev_is_comma || next_is_comma) continue;
            if (w>0 && out[w-1]==' ') continue;
            out[w++]=' ';
        } else out[w++]=(char)c;
    }
    out[w]=0; strncpy(s,out,MAXKEYLEN); s[MAXKEYLEN]=0;
}

/* ก็อปฐานข้อมูลเดิม (จับคู่ name,tel) มาไว้ใน overlay เพื่อให้ PUT แก้ไขได้ */
static void init_overlay_if_needed(const char **base_db){
    if (overlay_inited) return;
    int i=0;
    for (const char **p=base_db; *p && *(p+1); p+=2){
        strncpy(overlay_db[i].name, *p, sizeof(overlay_db[i].name)-1);
        strncpy(overlay_db[i].tel,  *(p+1), sizeof(overlay_db[i].tel)-1);
        overlay_db[i].name[sizeof(overlay_db[i].name)-1]=0;
        overlay_db[i].tel[sizeof(overlay_db[i].tel)-1]=0;
        i++;
        if (i >= (int)(sizeof(overlay_db)/sizeof(overlay_db[0]))-1) break;
    }
    overlay_db[i].name[0]=0; overlay_db[i].tel[0]=0; /* sentinel */
    overlay_inited = 1;
}

static const char* lookup_tel_overlay(const char* name){
    for (int i = 0; overlay_db[i].name[0]; i++)
        if (strcmp(name, overlay_db[i].name)==0) return overlay_db[i].tel;
    return "NoEntry";
}

/* อัปเดตเบอร์: คืนค่า "เบอร์เก่า" หรือ "NoEntry" ถ้าไม่พบ (ไม่สร้างใหม่) */
static const char* update_tel_overlay(const char* name, const char* new_tel){
    for (int i=0; overlay_db[i].name[0]; i++){
        if (strcmp(name, overlay_db[i].name)==0){
            static char old[32];
            strncpy(old, overlay_db[i].tel, sizeof(old)-1); old[sizeof(old)-1]=0;
            strncpy(overlay_db[i].tel, new_tel, sizeof(overlay_db[i].tel)-1);
            overlay_db[i].tel[sizeof(overlay_db[i].tel)-1]=0;
            return old;
        }
    }
    return "NoEntry";
}
/* ------------------------------------------------------------------------------ */

int main()
{
	int	socd;
	char	s_hostname[MAXHOSTNAME];
	struct hostent	*s_hostent;

	/* หาชื่อโฮสต์ของเซิร์ฟเวอร์และที่อยู่ IP (เก็บในโครงสร้าง hostent) */
	gethostname(s_hostname, sizeof(s_hostname));
	s_hostent = gethostbyname(s_hostname);

	/* ตั้งค่าเซิร์ฟเวอร์แบบ datagram (UDP) */
	socd = setup_dgserver(s_hostent, S_UDP_PORT);

	/* จัดการคำขอค้นหาข้อมูลจากลูกข่าย */
	db_search(socd);
	return 0;
}

int setup_dgserver(struct hostent *hostent, u_short port)
{
	int	socd;
	struct sockaddr_in	s_address;

	/* สร้าง socket แบบ SOCK_DGRAM (UDP) สำหรับโดเมนอินเทอร์เน็ต */
	if((socd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { perror("socket");exit(1); }

	/* สร้างโครงสร้างที่อยู่ (ที่อยู่ IP และหมายเลขพอร์ต) */
	bzero((char *)&s_address, sizeof(s_address));
	s_address.sin_family = AF_INET;
	s_address.sin_port = htons(port);
	bcopy((char *)hostent->h_addr, (char *)&s_address.sin_addr, hostent->h_length);

	/* กำหนดที่อยู่ให้กับ socket */
	if(bind(socd, (struct sockaddr *)&s_address, sizeof(s_address)) < 0) { perror("bind");exit(1); }

	return socd;
}

void db_search(int socd) /* ฟังก์ชันสำหรับจัดการคำขอค้นหาข้อมูลจากลูกข่าย */
{
	struct sockaddr_in	c_address;
	int	c_addrlen;
	char	key[MAXKEYLEN+1], data[MAXDATALEN+1];
	int	keylen, datalen;

    /* ---------- ฐานข้อมูลเดิม (คงไว้) ---------- */
	static char *db[] = {
        "amano-taro","0426-91-9418","ishida-jiro","0426-91-9872",
        "ueda-saburo","0426-91-9265","ema-shiro","0426-91-7254",
        "ooishi-goro","0426-91-9618",NULL
    };
	char	**dbp;
    /* ------------------------------------------- */

	while(1) {
		/* อ่านคีย์จาก socket */
		c_addrlen = sizeof(c_address);
		if((keylen = recvfrom(socd, key, MAXKEYLEN, 0, (struct sockaddr *)&c_address, &c_addrlen)) < 0) {
			perror("recvfrom");
			exit(1);
		}
		key[keylen] = '\0';
		printf("Received key> %s\n",key);

        /* ปรับแต่งอินพุต (โค้ดเดิมของคุณ) */
        {
            char cleaned[MAXKEYLEN + 1];
            int wi = 0;
            int len_norm = (int)strlen(key);

            for (int i = 0; i < len_norm; i++) {
                unsigned char c = (unsigned char)key[i];
                if (isspace(c)) {
                    int j = i - 1;
                    while (j >= 0 && isspace((unsigned char)key[j])) j--;
                    int k = i + 1;
                    while (k < len_norm && isspace((unsigned char)key[k])) k++;

                    int prev_is_comma = (j < 0) ? 1 : (key[j] == ',');
                    int next_is_comma = (k >= len_norm) ? 1 : (key[k] == ',');

                    if (prev_is_comma || next_is_comma) {
                        continue;
                    } else {
                        if (wi > 0 && cleaned[wi - 1] == ' ') continue;
                        cleaned[wi++] = ' ';
                    }
                } else {
                    cleaned[wi++] = (char)c;
                }
            }
            cleaned[wi] = '\0';
            strncpy(key, cleaned, MAXKEYLEN);
            key[MAXKEYLEN] = '\0';
        }

        /* [ADD] ทำความสะอาด comma แบบละเอียดเพิ่ม */
        compact_commas(key);
        char *msg = trim(key);

        /* [ADD] เตรียม overlay จากฐานข้อมูลเดิม (ทำครั้งแรกครั้งเดียว) */
        init_overlay_if_needed((const char**)db);

        /* [ADD] โปรโตคอล PUT: อัปเดตและส่งคืน "เบอร์เก่า" */
        if (strncmp(msg, "PUT:", 4) == 0) {
            char *payload = trim(msg + 4);
            char *comma = strchr(payload, ',');
            if (!comma) {
                strncpy(data, "ERR:PUT needs name,tel", MAXDATALEN-1);
                data[MAXDATALEN-1] = 0;
            } else {
                *comma = 0;
                char *name = trim(payload);
                char *tel  = trim(comma + 1);
                const char *old = update_tel_overlay(name, tel);  /* อัปเดตใน overlay */
                strncpy(data, old, MAXDATALEN-1);
                data[MAXDATALEN-1] = 0;
            }

            /* ส่งกลับและไปอ่านคำสั่งถัดไป */
            datalen = strlen(data);
            if(sendto(socd, data, datalen, 0, (struct sockaddr *)&c_address, c_addrlen) != datalen) {
                fprintf(stderr, "datagram error\n"); 
                exit(1);
            }
            printf("Sent data> %s\n", data);
            continue;
        }

        /* ถ้าเป็น GET: ตัด prefix ออก ให้ทำงานด้วยบล็อกค้นหาเดิม */
        if (strncmp(msg, "GET:", 4) == 0) {
            memmove(key, msg + 4, strlen(msg + 4) + 1);
        }

		/* ---------- รองรับหลายคีย์คั่นด้วยเครื่องหมายจุลภาค (บล็อกเดิม) ---------- */
		{
		    char *token;
		    int first = 1;                 /* ตัวช่วยใส่เครื่องหมายคั่น , ระหว่างผลลัพธ์ */

		    data[0] = '\0';               /* เคลียร์บัฟเฟอร์ผลลัพธ์ */

		    token = strtok(key, ",");    /* ตัดคีย์ด้วยเครื่องหมายจุลภาค */
		    while (token) {
		        /* ตัดช่องว่างหัว-ท้ายโทเคน */
		        token = trim(token);

                /* [ADD] ลองค้นจาก overlay ก่อน */
                const char *tel = lookup_tel_overlay(token);

                /* ถ้า overlay ไม่มี -> ใช้โค้ดค้นหาเดิมใน db ตัวอักษรจับคู่ */
                if (strcmp(tel, "NoEntry") == 0) {
		            dbp = db;
		            while (*dbp) {
		                if (strcmp(token, *dbp) == 0) {   /* ตรงชื่อในฐานข้อมูล */
		                    tel = *(dbp + 1);             /* ค่าโทรศัพท์ถัดไปในอาเรย์ */
		                    break;
		                }
		                dbp += 2;                         /* ขยับครั้งละ 2 (name, tel) */
		            }
                    if (!*dbp) tel = "NoEntry";
                }

		        /* ต่อผลลัพธ์ลงใน data โดยคั่นด้วย , หากไม่ใช่ตัวแรก */
		        if (!first) {
		            strncat(data, ",", MAXDATALEN - 1 - strlen(data));
		        }
		        strncat(data, tel, MAXDATALEN - 1 - strlen(data));
		        first = 0;

		        token = strtok(NULL, ",");
		    }

		    /* หากไม่มีโทเคนเลย (ผู้ใช้ส่งสตริงว่าง) ให้ตอบกลับว่า No entry */
		    if (data[0] == '\0') {
		        strncpy(data, "No entry", MAXDATALEN - 1);
		        data[MAXDATALEN - 1] = '\0';
		    }
		}
		/* -------------------------------------------------------------------- */
	
		/* ส่งข้อมูลที่ค้นหาได้กลับไปยัง socket */
		datalen = strlen(data);
		if(sendto(socd, data, datalen, 0, (struct sockaddr *)&c_address, c_addrlen) != datalen) {
			fprintf(stderr, "datagram error\n"); 
			exit(1);
		}
		printf("Sent data> %s\n", data);
	}
}