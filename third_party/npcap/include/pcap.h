/*
 * Минимальный заголовок pcap (ABI WinPcap/Npcap) для NetAnomalyAnalyzer.
 *
 * Используется, когда Npcap SDK не установлен: объявляет ровно те функции
 * и структуры, которые нужны приложению, с ABI, совместимым с wpcap.dll
 * из состава Npcap. Импортная библиотека генерируется из установленной
 * System32\wpcap.dll утилитами gendef+dlltool (см. README).
 *
 * Альтернатива: официальный Npcap SDK (-DNPCAP_SDK_DIR=путь_к_SDK) —
 * тогда этот заголовок не используется.
 */
#ifndef NETSCANNER_MINIMAL_PCAP_H
#define NETSCANNER_MINIMAL_PCAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_ERRBUF_SIZE 256

typedef unsigned int  bpf_u_int32;
typedef unsigned char u_char;

/* Платформонезависимый аналог struct timeval (long = 4 байта на Windows):
 * макет идентичен timeval из winsock2, поэтому заполнение из wpcap.dll
 * корректно, а имя не конфликтует с системными заголовками. */
struct pcap_timeval {
    long tv_sec;
    long tv_usec;
};

struct pcap_pkthdr {
    struct pcap_timeval ts;
    bpf_u_int32 caplen;
    bpf_u_int32 len;
};

struct bpf_insn; /* непрозрачно */

struct bpf_program {
    unsigned int       bf_len;
    struct bpf_insn   *bf_insns;
};

struct pcap_addr; /* непрозрачно */

struct pcap_if {
    struct pcap_if *next;
    char           *name;
    char           *description;
    struct pcap_addr *addresses;
    unsigned int     flags;
};
typedef struct pcap_if pcap_if_t;

struct pcap; /* непрозрачно */
typedef struct pcap pcap_t;

/* Флаги устройств */
#define PCAP_IF_LOOPBACK 0x00000008
#define PCAP_IF_UP       0x00000001
#define PCAP_IF_RUNNING  0x00000002
#define PCAP_IF_WIRELESS 0x00000004

#define PCAP_NETMASK_UNKNOWN 0xffffffff

int  pcap_findalldevs(pcap_if_t **alldevsp, char *errbuf);
void pcap_freealldevs(pcap_if_t *alldevs);

pcap_t *pcap_open_live(const char *device, int snaplen, int promisc,
                       int to_ms, char *errbuf);
void pcap_close(pcap_t *p);
int  pcap_next_ex(pcap_t *p, struct pcap_pkthdr **pkt_header,
                  const unsigned char **pkt_data);

int  pcap_compile(pcap_t *p, struct bpf_program *fp, const char *str,
                  int optimize, bpf_u_int32 netmask);
int  pcap_setfilter(pcap_t *p, struct bpf_program *fp);
void pcap_freecode(struct bpf_program *fp);
char *pcap_geterr(pcap_t *p);

#ifdef __cplusplus
}
#endif

#endif /* NETSCANNER_MINIMAL_PCAP_H */
