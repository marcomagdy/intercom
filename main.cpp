#include <arpa/inet.h>
#include <atomic>
#include <iostream>
#include <netinet/in.h>
#include <portaudio.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#define CHUNK_SIZE 1024
#define SAMPLE_RATE 44100
#define CHANNELS 1
#define DISCOVERY_PORT 5000
#define AUDIO_PORT 5001
#define BROADCAST_ADDR "255.255.255.255"

class Intercom {
private:
    PaStream* stream;
    int udp_sock;
    int audio_sock;
    std::string remote_ip;
    std::atomic<bool> running;
    std::atomic<bool> connected;
    bool is_recording;

    static int audioCallback(const void* input, void* output, unsigned long frameCount,
        const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData)
    {
        Intercom* intercom = static_cast<Intercom*>(userData);
        if (intercom->is_recording) {
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(AUDIO_PORT);
            inet_pton(AF_INET, intercom->remote_ip.c_str(), &addr.sin_addr);
            sendto(intercom->audio_sock, input, frameCount * sizeof(int16_t), 0, (struct sockaddr*)&addr, sizeof(addr));
        } else {
            char buffer[CHUNK_SIZE * 2];
            struct sockaddr_in sender;
            socklen_t len = sizeof(sender);
            int received = recvfrom(intercom->audio_sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender, &len);
            if (received > 0 && sender.sin_addr.s_addr == inet_addr(intercom->remote_ip.c_str())) {
                memcpy(output, buffer, received);
            }
        }
        return paContinue;
    }

    std::string getLocalIP()
    {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(9);
        inet_pton(AF_INET, "10.255.255.255", &addr.sin_addr);
        connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        struct sockaddr_in local;
        socklen_t len = sizeof(local);
        getsockname(sock, (struct sockaddr*)&local, &len);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
        close(sock);
        return std::string(ip);
    }

    void discoveryListener()
    {
        char buffer[1024];
        while (!connected) {
            struct sockaddr_in sender;
            socklen_t len = sizeof(sender);
            int received = recvfrom(udp_sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender, &len);
            if (received > 0) {
                std::string message(buffer, received);
                if (message.find("INTERCOM:") == 0 && sender.sin_addr.s_addr != inet_addr(getLocalIP().c_str())) {
                    remote_ip = message.substr(9);
                    connected = true;
                    std::cout << "Connected to " << remote_ip << std::endl;
                }
            }
        }
    }

public:
    Intercom()
        : running(false)
        , connected(false)
        , is_recording(false)
    {
        // Initialize PortAudio
        Pa_Initialize();

        // Setup UDP discovery socket
        udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        int broadcast = 1;
        setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
        struct sockaddr_in udp_addr;
        udp_addr.sin_family = AF_INET;
        udp_addr.sin_port = htons(DISCOVERY_PORT);
        udp_addr.sin_addr.s_addr = INADDR_ANY;
        bind(udp_sock, (struct sockaddr*)&udp_addr, sizeof(udp_addr));

        // Setup audio socket
        audio_sock = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in audio_addr;
        audio_addr.sin_family = AF_INET;
        audio_addr.sin_port = htons(AUDIO_PORT);
        audio_addr.sin_addr.s_addr = INADDR_ANY;
        bind(audio_sock, (struct sockaddr*)&audio_addr, sizeof(audio_addr));
    }

    ~Intercom()
    {
        Pa_Terminate();
        close(udp_sock);
        close(audio_sock);
    }

    void discover()
    {
        std::cout << "Starting discovery..." << std::endl;
        std::thread listener(&Intercom::discoveryListener, this);
        listener.detach();

        std::string message = "INTERCOM:" + getLocalIP();
        struct sockaddr_in broadcast_addr;
        broadcast_addr.sin_family = AF_INET;
        broadcast_addr.sin_port = htons(DISCOVERY_PORT);
        inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);

        while (!connected) {
            sendto(udp_sock, message.c_str(), message.length(), 0, (struct sockaddr*)&broadcast_addr,
                sizeof(broadcast_addr));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void run()
    {
        discover();

        // Setup audio stream
        Pa_OpenDefaultStream(&stream, CHANNELS, CHANNELS, paInt16, SAMPLE_RATE, CHUNK_SIZE, audioCallback, this);
        Pa_StartStream(stream);

        while (true) {
            std::string input;
            std::cout << "Press 't' to talk, 'l' to listen, 'q' to quit: ";
            std::getline(std::cin, input);

            if (input == "q")
                break;
            else if (input == "t" || input == "l") {
                is_recording = (input == "t");
                running = true;
                std::cout << (is_recording ? "Recording..." : "Playing...") << " Press Enter to stop\n";
                std::cin.get();
                running = false;
            }
        }

        Pa_StopStream(stream);
        Pa_CloseStream(stream);
    }
};

int main()
{
    Intercom intercom;
    intercom.run();
    return 0;
}
