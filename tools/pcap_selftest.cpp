/*
 * Самодиагностика pcap: перечисление устройств и тест захвата.
 *
 * Использование:
 *   PcapSelfTest                 — список доступных интерфейсов
 *   PcapSelfTest <устройство> [секунд] — захват на устройстве (по умолчанию 5 с)
 */
#include <pcap.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>

int main(int argc, char *argv[])
{
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    pcap_if_t *devices = nullptr;

    if (pcap_findalldevs(&devices, errbuf) == -1 || !devices) {
        std::fprintf(stderr, "pcap_findalldevs failed: %s\n", errbuf);
        return 1;
    }

    std::printf("Devices found:\n");
    for (pcap_if_t *dev = devices; dev; dev = dev->next) {
        std::printf("  %-55s %s%s\n", dev->name,
                    dev->description ? dev->description : "(no description)",
                    (dev->flags & PCAP_IF_LOOPBACK) ? "  [loopback]" : "");
    }

    if (argc < 2) {
        pcap_freealldevs(devices);
        return 0;
    }

    const std::string wanted = argv[1];
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 5;

    pcap_if_t *chosen = nullptr;
    for (pcap_if_t *dev = devices; dev; dev = dev->next) {
        if (wanted == dev->name || wanted == (dev->description ? dev->description : "")) {
            chosen = dev;
            break;
        }
    }
    if (!chosen) {
        std::fprintf(stderr, "Device '%s' not found\n", wanted.c_str());
        pcap_freealldevs(devices);
        return 2;
    }

    pcap_t *handle = pcap_open_live(chosen->name, 65535, 1, 500, errbuf);
    if (!handle) {
        std::fprintf(stderr, "pcap_open_live failed: %s\n", errbuf);
        pcap_freealldevs(devices);
        return 3;
    }

    std::printf("Capturing on %s for %d s...\n", chosen->name, seconds);
    std::fflush(stdout);

    long long packets = 0, bytes = 0;
    std::unordered_map<std::string, long long> byPort;
    const auto start = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
        struct pcap_pkthdr *header = nullptr;
        const unsigned char *data = nullptr;
        const int rc = pcap_next_ex(handle, &header, &data);
        if (rc <= 0 || !header)
            continue;
        ++packets;
        bytes += header->caplen;

        // Лёгкая разбивка: Ethernet(14) -> IPv4: протокол и порты
        if (header->caplen >= 34 && data[12] == 0x08 && data[13] == 0x00) {
            const unsigned char proto = data[23];
            const int ihl = (data[14] & 0x0F) * 4;
            const int l4 = 14 + ihl;
            if (proto == 6 && header->caplen >= l4 + 4)
                byPort["TCP/" + std::to_string((data[l4] << 8) | data[l4 + 1]) +
                       (data[l4 + 2] > 0 ? "->" + std::to_string((data[l4 + 2] << 8) | data[l4 + 3])
                                         : "")] += 1;
            else if (proto == 17 && header->caplen >= l4 + 4)
                byPort["UDP/" + std::to_string((data[l4] << 8) | data[l4 + 1])] += 1;
            else if (proto == 1)
                byPort["ICMP"] += 1;
            else
                byPort["IP/" + std::to_string(proto)] += 1;
        } else {
            byPort["non-IPv4"] += 1;
        }
    }
    pcap_close(handle);
    pcap_freealldevs(devices);

    std::printf("RESULT packets=%lld bytes=%lld\n", packets, bytes);
    for (const auto &kv : byPort)
        std::printf("  %-24s %lld\n", kv.first.c_str(), kv.second);
    return packets > 0 ? 0 : 4;
}
