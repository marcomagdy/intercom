#include <iostream>
#include <portaudio.h>
#include <unistd.h>

#include "TcpConnection.h"

#define CHUNK_SIZE 1024
#define SAMPLE_RATE 44100
#define TCP_PORT 6879

int recordCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData)
{
    (void)outputBuffer; // Prevent unused variable warning
    (void)timeInfo;
    (void)statusFlags;
    if (inputBuffer == nullptr) {
        return paContinue; // No input data
    }
    auto* connection = static_cast<Intercom::TcpConnection*>(userData);
    auto [written, err] = connection->write((uint8_t*)inputBuffer, framesPerBuffer * sizeof(int16_t));
    if (err == EWOULDBLOCK) {
        return paContinue; // Socket is full, continue recording
    }
    if (err != 0) {
        std::cerr << "Error writing to socket: " << strerror(err) << std::endl;
        return paComplete; // Stop recording on error
    }
    return paContinue;
}

int playCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData)
{
    (void)inputBuffer; // Prevent unused variable warning
    (void)timeInfo;
    (void)statusFlags;
    auto* connection = static_cast<Intercom::TcpConnection*>(userData);
    auto [read, err] = connection->read_once(static_cast<uint8_t*>(outputBuffer), framesPerBuffer * sizeof(int16_t));
    if (err == EWOULDBLOCK) {
        memset(outputBuffer, 0, framesPerBuffer * sizeof(int16_t)); // Fill with silence
        return paContinue;                                          // No data available
    }
    if (err != 0) {
        std::cerr << "Error reading from socket: " << strerror(err) << std::endl;
        return paComplete; // Stop playback on error
    }

    if (read == 0) {                                                // other side closed the connection
        memset(outputBuffer, 0, framesPerBuffer * sizeof(int16_t)); // Fill with silence
        return paComplete;                                          // Stop playback
    }

    // Fill the rest of the output buffer with silence
    if ((size_t)read < framesPerBuffer * sizeof(int16_t)) {
        memset(static_cast<uint8_t*>(outputBuffer) + read, 0, (framesPerBuffer * sizeof(int16_t)) - read);
    }
    return paContinue; // Continue playback
}

class IntercomAudio
{
    PaStream* m_recording_stream;
    PaStream* m_playback_stream;
public:
    IntercomAudio(PaStream* rec_stream, PaStream* play_stream)
        : m_recording_stream(rec_stream)
        , m_playback_stream(play_stream)
    {
    }

    ~IntercomAudio()
    {
        if (m_recording_stream) {
            Pa_AbortStream(m_recording_stream);
            Pa_CloseStream(m_recording_stream);
        }
        if (m_playback_stream) {
            Pa_AbortStream(m_playback_stream);
            Pa_CloseStream(m_playback_stream);
        }
    }

    IntercomAudio(const IntercomAudio&) = delete;
    IntercomAudio& operator=(const IntercomAudio&) = delete;
    IntercomAudio(IntercomAudio&& other) : m_recording_stream(other.m_recording_stream)
        , m_playback_stream(other.m_playback_stream)
    {
        other.m_recording_stream = nullptr;
        other.m_playback_stream = nullptr;
    }

    IntercomAudio& operator=(IntercomAudio&&) = delete;

    static std::optional<IntercomAudio> create(Intercom::TcpConnection& connection)
    {
        PaStreamParameters inputParameters;
        inputParameters.device = Pa_GetDefaultInputDevice();
        inputParameters.channelCount = 1;
        inputParameters.sampleFormat = paInt16;
        inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
        inputParameters.hostApiSpecificStreamInfo = nullptr;

        PaStream* rec_stream;
        auto err
            = Pa_OpenStream(&rec_stream, &inputParameters, nullptr, SAMPLE_RATE, CHUNK_SIZE, paClipOff, recordCallback, &connection);
        if (err != paNoError) {
            std::cerr << "Error opening recording stream: " << Pa_GetErrorText(err) << std::endl;
            return std::nullopt;
        }



        PaStreamParameters outputParameters;
        outputParameters.device = Pa_GetDefaultOutputDevice();
        outputParameters.channelCount = 1;
        outputParameters.sampleFormat = paInt16;
        outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = nullptr;


        PaStream* play_stream;
        err = Pa_OpenStream(&play_stream, nullptr, &outputParameters, SAMPLE_RATE, CHUNK_SIZE, paClipOff, playCallback, &connection);
        if (err != paNoError) {
            std::cerr << "Error opening playback stream: " << Pa_GetErrorText(err) << std::endl;
            return std::nullopt;
        }

        return IntercomAudio(rec_stream, play_stream);
    }

    void start_recording()
    {
        auto err = Pa_StartStream(m_recording_stream);
        if (err != paNoError) {
            std::cerr << "Error starting recording stream: " << Pa_GetErrorText(err) << std::endl;
        }
    }

    void stop_recording()
    {
        auto err = Pa_AbortStream(m_recording_stream);
        if (err != paNoError) {
            std::cerr << "Error stopping recording stream: " << Pa_GetErrorText(err) << std::endl;
        }
    }

    void start_playback()
    {
        auto err = Pa_StartStream(m_playback_stream);
        if (err != paNoError) {
            std::cerr << "Error starting playback stream: " << Pa_GetErrorText(err) << std::endl;
        }
    }

    void stop_playback()
    {
        auto err = Pa_AbortStream(m_playback_stream);
        if (err != paNoError) {
            std::cerr << "Error stopping playback stream: " << Pa_GetErrorText(err) << std::endl;
        }
    }

};

int extract_pid(std::string message)
{
    size_t pos = message.find("Hello");
    if (pos == std::string::npos) {
        return -1; // "Hello" not found
    }
    pos += strlen("Hello");
    while (pos < message.size() && isspace(message[pos])) {
        pos++;
    }
    if (pos >= message.size()) {
        return -1; // No PID found
    }
    size_t end_pos = message.find_first_not_of("0123456789", pos);
    if (end_pos == std::string::npos) {
        end_pos = message.size();
    }
    std::string pid_str = message.substr(pos, end_pos - pos);
    int pid = std::stoi(pid_str);
    return pid;
}

std::string dicover_peer(bool& should_listen)
{
    constexpr uint16_t kBroadcastPort = 55430;
    auto optSocket = Intercom::UdpSocket::create(kBroadcastPort);

    const char* broadcast_message = "Hello %d";
    char outgoing_message[256];
    const auto pid = getpid();
    snprintf(outgoing_message, sizeof(outgoing_message), broadcast_message, pid);
    std::string peer_ip_address;
    if (!optSocket) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        return peer_ip_address;
    }
    auto& broadcastSocket = *optSocket;

    while (true) {

        broadcastSocket.broadcast((uint8_t*)outgoing_message, strlen(outgoing_message));
        std::string sender_address;
        char incoming_msg[256];
        auto [read, err] = broadcastSocket.receive_from((uint8_t*)incoming_msg, sizeof(incoming_msg), sender_address);
        if (err != 0) {
            std::cerr << "Error receiving broadcast: " << strerror(err) << std::endl;
            continue;
        }

        if (read > 0) {
            incoming_msg[read] = '\0'; // Null-terminate the received string
            // Ignore my own message
            if (strcmp(incoming_msg, outgoing_message) == 0) {
                continue;
            }

            int peer_pid = extract_pid(incoming_msg);
            if (peer_pid < 1) {
                continue;
            }
            should_listen = pid < peer_pid;

            printf("Received broadcast from %s: %s\n", sender_address.c_str(), incoming_msg);
            peer_ip_address = sender_address;
            break;
        }
        usleep(100000); // Sleep for 100ms
    }

    if (peer_ip_address.empty()) {
        std::cerr << "Failed to discover peer" << std::endl;
    } else {
        printf("Discovered peer: %s\n", peer_ip_address.c_str());
    }
    return peer_ip_address;
}

int main(int argc, char* argv[])
{
    bool should_listen = false;
    std::string peer_ip_address = dicover_peer(should_listen);
    if (peer_ip_address.empty()) {
        fprintf(stderr, "Failed to discover peer\n");
        return -1;
    }
    if (should_listen) {
        printf("Listening for incoming connections...\n");
    } else {
        printf("Connecting to peer...\n");
    }

    
    Pa_Initialize();
    std::optional<Intercom::TcpConnection> connection;
    if (should_listen) {
        // Start server
        printf("Starting server...\n");
        auto optListener = Intercom::TcpConnectionListener::listen(TCP_PORT);
        if (!optListener) {
            std::cerr << "Failed to start listener" << std::endl;
            return -1;
        }
        auto& listener = *optListener;

        auto optConn = listener.accept();
        if (!optConn) {
            std::cerr << "Failed to accept connection" << std::endl;
            return -1;
        }
        optConn->set_non_blocking();
        connection = std::move(*optConn);
    } else {
        // Start client
        printf("Starting client...\n");
        // Client code here
        printf("Connecting to [%s]...\n", argv[2]);
        if (auto optConn = Intercom::TcpConnection::connect(argv[2], TCP_PORT); optConn) {
            optConn->set_non_blocking();
            connection = std::move(*optConn);
        }
        else {
            std::cerr << "Failed to connect to server" << std::endl;
            return -1;
        }
    }

    auto optIntercomAudio = IntercomAudio::create(*connection);
    if (!optIntercomAudio) {
        std::cerr << "Failed to create audio stream" << std::endl;
        return -1;
    }
    auto& intercomAudio = *optIntercomAudio;
    intercomAudio.start_playback();
    bool recording = false;
    while (true) {
        printf("Press ' ' to toggle recording/playback, 'q' to quit\n");
        std::string input;
        std::getline(std::cin, input);
        if (input.empty()) {
            continue;
        }
        intercomAudio.stop_recording();
        intercomAudio.stop_playback();
        char ch = input[0];
        if (ch  == ' ') {
            if (recording) {
                printf("stop recording, start playback\n");
                intercomAudio.start_playback();
            }
            else {
                printf("start recording, stop playback\n");
                intercomAudio.start_recording();
            }
            recording = !recording;
        }
        else if (ch == 'q') {
            optIntercomAudio.reset();
            break;
        }
    }
    Pa_Terminate();
}
