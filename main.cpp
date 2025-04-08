#include <iostream>
#include <portaudio.h>

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

void SpeakerMain(Intercom::TcpConnection& conn)
{
    printf("Connected.. \n");
    // Record audio from microphone for 5s and then play it back.
    PaStreamParameters inputParameters;
    inputParameters.device = Pa_GetDefaultInputDevice();
    inputParameters.channelCount = 1;
    inputParameters.sampleFormat = paInt16;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;
    PaStream* stream;
    auto err
        = Pa_OpenStream(&stream, &inputParameters, nullptr, SAMPLE_RATE, CHUNK_SIZE, paClipOff, recordCallback, &conn);
    if (err != paNoError) {
        std::cerr << "Error opening stream: " << Pa_GetErrorText(err) << std::endl;
        return;
    }
    Pa_StartStream(stream);
    // read a character from stdin and if it's q then quit
    char c;
    while (true) {
        std::cin >> c;
        if (c == 'q') {
            break;
        }
    }
    Pa_CloseStream(stream);
}

void ListenerMain()
{
    auto optListener = Intercom::TcpConnectionListener::listen(TCP_PORT);
    if (!optListener) {
        std::cerr << "Failed to start listener" << std::endl;
        return;
    }
    auto& listener = *optListener;

    auto optConn = listener.accept();
    if (!optConn) {
        std::cerr << "Failed to accept connection" << std::endl;
        return;
    }
    auto& conn = *optConn;

    PaStreamParameters outputParameters;
    outputParameters.device = Pa_GetDefaultOutputDevice();
    printf("Output device: %s\n", Pa_GetDeviceInfo(outputParameters.device)->name);
    outputParameters.channelCount = 1;
    outputParameters.sampleFormat = paInt16;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;
    printf("Playing back audio...\n");
    PaStream* stream;
    auto err
        = Pa_OpenStream(&stream, nullptr, &outputParameters, SAMPLE_RATE, CHUNK_SIZE, paClipOff, playCallback, &conn);
    if (err != paNoError) {
        std::cerr << "Error opening stream: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    Pa_StartStream(stream);
    // read a character from stdin and if it's q then quit
    char c;
    while (true) {
        std::cin >> c;
        if (c == 'q') {
            break;
        }
    }
    Pa_CloseStream(stream);
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s (server|client) hostname\n", argv[0]);
        return -1;
    }

    Pa_Initialize();
    if (strcmp(argv[1], "server") == 0) {
        // Start server
        printf("Starting server...\n");
        ListenerMain();
    } else if (strcmp(argv[1], "client") == 0) {
        // Start client
        printf("Starting client...\n");
        // Client code here
        printf("Connecting to [%s]...\n", argv[2]);
        if (auto optConn = Intercom::TcpConnection::connect(argv[2], TCP_PORT); optConn) {
            SpeakerMain(*optConn);
        }
    } else {
        fprintf(stderr, "Invalid mode. Use 'server' or 'client'.\n");
        return -1;
    }

    Pa_Terminate();
}
