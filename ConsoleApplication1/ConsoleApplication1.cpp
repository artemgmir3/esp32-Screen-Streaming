#include <iostream>
#include <vector>
#include <thread>
#include <winsock2.h>
#include <windows.h>
#include <gdiplus.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

constexpr int PORT = 5451;
constexpr int WIDTH = 320; // ширина изображения
constexpr int HEIGHT = 240; // высота изображения
constexpr long JPEG_QUALITY = 50L;
constexpr BYTE FOOTER[4] = { 0x55, 0x44, 0x55, 0x11 };

Gdiplus::GdiplusStartupInput gdiplusStartupInput;
ULONG_PTR gdiplusToken;

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::ImageCodecInfo* pImageCodecInfo = nullptr;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == nullptr) return -1;

    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

bool captureScreenshot(std::vector<BYTE>& buffer) {
    using namespace Gdiplus;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    SelectObject(hdcMem, hBitmap);

    BitBlt(hdcMem, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), hdcScreen, 0, 0, SRCCOPY);

    Bitmap bitmap(hBitmap, nullptr);
    Bitmap resizedBitmap(WIDTH, HEIGHT, PixelFormat24bppRGB);
    Graphics graphics(&resizedBitmap);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(&bitmap, 0, 0, WIDTH, HEIGHT);

    IStream* stream = nullptr;
    CreateStreamOnHGlobal(nullptr, TRUE, &stream);

    CLSID encoderClsid;
    GetEncoderClsid(L"image/jpeg", &encoderClsid);

    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    encoderParams.Parameter[0].Value = reinterpret_cast<void*>(const_cast<long*>(&JPEG_QUALITY));

    resizedBitmap.Save(stream, &encoderClsid, &encoderParams);

    LARGE_INTEGER liSize;
    stream->Seek({ 0 }, STREAM_SEEK_END, (ULARGE_INTEGER*)&liSize);
    stream->Seek({ 0 }, STREAM_SEEK_SET, nullptr);

    buffer.resize((size_t)liSize.QuadPart + sizeof(FOOTER));
    stream->Read(buffer.data(), (ULONG)liSize.QuadPart, nullptr);
    memcpy(buffer.data() + liSize.QuadPart, FOOTER, sizeof(FOOTER));

    stream->Release();
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    std::cout << "Screenshot captured and resized" << std::endl;
    return true;
}

void handleClient(SOCKET clientSocket) {
    std::vector<BYTE> buffer;
    BYTE data[4];

    while (true) {
        int bytesRead = recv(clientSocket, (char*)data, sizeof(data), 0);
        if (bytesRead == 4 && memcmp(data, FOOTER, sizeof(FOOTER)) == 0) {
            std::cout << "Request received" << std::endl;
            if (captureScreenshot(buffer)) {
                send(clientSocket, (char*)buffer.data(), (int)buffer.size(), 0);
                std::cout << "Frame sent" << std::endl;
            }
        }
        else {
            std::cout << "Received data: " << bytesRead << " bytes" << std::endl;
        }
    }

    std::cout << "Client disconnected" << std::endl;
    closesocket(clientSocket);
}

int main() {
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr = { 0 };
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    std::cout << "Server running" << std::endl;

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        std::cout << "Client connected" << std::endl;
        std::thread(handleClient, clientSocket).detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}
