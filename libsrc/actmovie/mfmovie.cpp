#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include <math.h>
#include <wchar.h>

#include <appagg.h>
#include <wappapi.h>

#include "mfmovie.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "evr.lib")

// Linking strmiids.lib would collide with the engine's legacy DirectShow GUID
// definitions, so keep the one EVR service identifier needed here local.
static const GUID kVideoRenderService = {
    0x1092a86c, 0xab1a, 0x459a,
    {0xa3, 0x36, 0x83, 0x1f, 0xbc, 0x4d, 0x11, 0xff}
};

template <class T>
static void ReleaseInterface(T *&object)
{
    if (object)
    {
        object->Release();
        object = NULL;
    }
}

static BOOL FindModernMovie(const char *legacyPath, WCHAR *modernPath,
                            size_t modernPathCount)
{
    WCHAR *extension;
    if (!legacyPath ||
        !MultiByteToWideChar(CP_ACP, 0, legacyPath, -1, modernPath,
                             (int)modernPathCount))
        return FALSE;

    extension = wcsrchr(modernPath, L'.');
    if (!extension || extension + 5 > modernPath + modernPathCount)
        return FALSE;
    wcscpy(extension, L".mp4");
    return GetFileAttributesW(modernPath) != INVALID_FILE_ATTRIBUTES;
}

static HRESULT AddTopologyBranch(IMFTopology *topology, IMFMediaSource *source,
                                 IMFPresentationDescriptor *presentation,
                                 IMFStreamDescriptor *stream, HWND hwnd)
{
    IMFMediaTypeHandler *handler = NULL;
    IMFActivate *renderer = NULL;
    IMFTopologyNode *sourceNode = NULL;
    IMFTopologyNode *outputNode = NULL;
    GUID majorType = GUID_NULL;
    HRESULT result = stream->GetMediaTypeHandler(&handler);
    if (SUCCEEDED(result))
        result = handler->GetMajorType(&majorType);

    if (SUCCEEDED(result) && majorType == MFMediaType_Video)
        result = MFCreateVideoRendererActivate(hwnd, &renderer);
    else if (SUCCEEDED(result) && majorType == MFMediaType_Audio)
        result = MFCreateAudioRendererActivate(&renderer);
    else if (SUCCEEDED(result))
        result = MF_E_INVALIDMEDIATYPE;

    if (SUCCEEDED(result))
        result = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE,
                                      &sourceNode);
    if (SUCCEEDED(result))
        result = sourceNode->SetUnknown(MF_TOPONODE_SOURCE, source);
    if (SUCCEEDED(result))
        result = sourceNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR,
                                        presentation);
    if (SUCCEEDED(result))
        result = sourceNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, stream);
    if (SUCCEEDED(result))
        result = topology->AddNode(sourceNode);

    if (SUCCEEDED(result))
        result = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &outputNode);
    if (SUCCEEDED(result))
        result = outputNode->SetObject(renderer);
    if (SUCCEEDED(result))
        result = outputNode->SetUINT32(MF_TOPONODE_STREAMID, 0);
    if (SUCCEEDED(result))
        result = outputNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
    if (SUCCEEDED(result))
        result = topology->AddNode(outputNode);
    if (SUCCEEDED(result))
        result = sourceNode->ConnectOutput(0, outputNode, 0);

    ReleaseInterface(outputNode);
    ReleaseInterface(sourceNode);
    ReleaseInterface(renderer);
    ReleaseInterface(handler);
    return result;
}

static HRESULT BuildTopology(IMFMediaSource *source, HWND hwnd,
                             IMFTopology **topology)
{
    IMFPresentationDescriptor *presentation = NULL;
    HRESULT result = MFCreateTopology(topology);
    if (SUCCEEDED(result))
        result = source->CreatePresentationDescriptor(&presentation);

    DWORD streamCount = 0;
    if (SUCCEEDED(result))
        result = presentation->GetStreamDescriptorCount(&streamCount);

    for (DWORD index = 0; SUCCEEDED(result) && index < streamCount; ++index)
    {
        BOOL selected = FALSE;
        IMFStreamDescriptor *stream = NULL;
        result = presentation->GetStreamDescriptorByIndex(index, &selected,
                                                           &stream);
        if (SUCCEEDED(result) && selected)
        {
            result = AddTopologyBranch(*topology, source, presentation,
                                       stream, hwnd);
            if (result == MF_E_INVALIDMEDIATYPE)
            {
                presentation->DeselectStream(index);
                result = S_OK;
            }
        }
        ReleaseInterface(stream);
    }

    ReleaseInterface(presentation);
    return result;
}

static BOOL PumpMovieMessages()
{
    MSG message;
    while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            PostQuitMessage((int)message.wParam);
            return FALSE;
        }
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return !(GetAsyncKeyState(VK_ESCAPE) & 1) &&
           !(GetAsyncKeyState(VK_RETURN) & 1) &&
           !(GetAsyncKeyState(VK_SPACE) & 1) &&
           !(GetAsyncKeyState(VK_BACK) & 1) &&
           !(GetAsyncKeyState(VK_LBUTTON) & 1) &&
           !(GetAsyncKeyState(VK_RBUTTON) & 1);
}

static HRESULT WaitForTopology(IMFMediaSession *session)
{
    HRESULT result = S_OK;
    while (SUCCEEDED(result))
    {
        IMFMediaEvent *event = NULL;
        MediaEventType type = MEUnknown;
        result = session->GetEvent(0, &event);
        if (SUCCEEDED(result))
        {
            HRESULT eventStatus = S_OK;
            result = event->GetStatus(&eventStatus);
            if (SUCCEEDED(result))
                result = eventStatus;
        }
        if (SUCCEEDED(result))
            result = event->GetType(&type);
        if (SUCCEEDED(result) && type == MESessionTopologyStatus)
        {
            UINT32 status = 0;
            if (SUCCEEDED(event->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status)) &&
                status == MF_TOPOSTATUS_READY)
            {
                ReleaseInterface(event);
                return S_OK;
            }
        }
        ReleaseInterface(event);
    }
    return result;
}

static HRESULT RunSession(IMFMediaSession *session, HWND hwnd, int volume)
{
    IMFVideoDisplayControl *video = NULL;
    IMFSimpleAudioVolume *audio = NULL;
    PROPVARIANT startPosition;
    RECT clientRect;
    HRESULT result;

    result = WaitForTopology(session);
    if (FAILED(result))
        return result;

    result = MFGetService(session, kVideoRenderService,
                          IID_PPV_ARGS(&video));
    if (FAILED(result))
        return result;
    GetClientRect(hwnd, &clientRect);
    result = video->SetVideoPosition(NULL, &clientRect);
    if (FAILED(result))
    {
        ReleaseInterface(video);
        return result;
    }
    if (SUCCEEDED(MFGetService(session, MR_POLICY_VOLUME_SERVICE,
                               IID_PPV_ARGS(&audio))))
    {
        float scalar = volume <= -10000 ? 0.0f :
                       (float)pow(10.0, (double)volume / 2000.0);
        if (scalar > 1.0f)
            scalar = 1.0f;
        audio->SetMasterVolume(scalar);
    }
    ReleaseInterface(audio);

    PropVariantInit(&startPosition);
    result = session->Start(&GUID_NULL, &startPosition);
    PropVariantClear(&startPosition);
    if (FAILED(result))
    {
        ReleaseInterface(video);
        return result;
    }

    BOOL playing = TRUE;
    while (playing)
    {
        IMFMediaEvent *event = NULL;
        MediaEventType type = MEUnknown;
        result = session->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event);
        if (result == MF_E_NO_EVENTS_AVAILABLE)
        {
            if (!PumpMovieMessages())
            {
                session->Stop();
                break;
            }
            Sleep(10);
            continue;
        }
        if (FAILED(result))
            break;

        HRESULT eventStatus = S_OK;
        result = event->GetStatus(&eventStatus);
        if (SUCCEEDED(result))
            result = eventStatus;
        if (SUCCEEDED(result))
            result = event->GetType(&type);
        if (type == MESessionEnded || type == MESessionStopped ||
            type == MEError)
            playing = FALSE;
        ReleaseInterface(event);
    }
    ReleaseInterface(video);
    return result;
}

BOOL ModernMoviePlaySynchronous(const char *legacyPath, int volume)
{
    WCHAR moviePath[MAX_PATH];
    IMFSourceResolver *resolver = NULL;
    IUnknown *sourceObject = NULL;
    IMFMediaSource *source = NULL;
    IMFTopology *topology = NULL;
    IMFMediaSession *session = NULL;
    MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
    HRESULT result;
    BOOL mediaFoundationStarted = FALSE;
    BOOL played = FALSE;

    if (!FindModernMovie(legacyPath, moviePath,
                         sizeof(moviePath) / sizeof(moviePath[0])))
        return FALSE;

    AutoAppIPtr(WinApp);
    if (!pWinApp)
        return FALSE;

    result = MFStartup(MF_VERSION);
    if (SUCCEEDED(result))
    {
        mediaFoundationStarted = TRUE;
        result = MFCreateSourceResolver(&resolver);
    }
    if (SUCCEEDED(result))
        result = resolver->CreateObjectFromURL(
            moviePath, MF_RESOLUTION_MEDIASOURCE, NULL, &objectType,
            &sourceObject);
    if (SUCCEEDED(result))
        result = sourceObject->QueryInterface(IID_PPV_ARGS(&source));
    if (SUCCEEDED(result))
        result = BuildTopology(source, pWinApp->GetMainWnd(), &topology);
    if (SUCCEEDED(result))
        result = MFCreateMediaSession(NULL, &session);
    if (SUCCEEDED(result))
        result = session->SetTopology(0, topology);
    if (SUCCEEDED(result))
    {
        result = RunSession(session, pWinApp->GetMainWnd(), volume);
        played = SUCCEEDED(result);
    }

    if (session)
    {
        session->Close();
        session->Shutdown();
    }
    if (source)
        source->Shutdown();
    ReleaseInterface(session);
    ReleaseInterface(topology);
    ReleaseInterface(source);
    ReleaseInterface(sourceObject);
    ReleaseInterface(resolver);
    if (mediaFoundationStarted)
        MFShutdown();
    return played;
}
