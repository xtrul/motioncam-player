#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include <motioncam/Decoder.hpp>
#include <audiofile/AudioFile.h>
#define TINY_DNG_WRITER_IMPLEMENTATION
#include <tinydng/tiny_dng_writer.h>
#undef TINY_DNG_WRITER_IMPLEMENTATION

#include <nlohmann/json.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>

namespace fs = std::filesystem;

static void writeAudio(const std::string& path, int sampleRate, int channels,
                       std::vector<motioncam::AudioChunk>& chunks) {
    AudioFile<int16_t> audio;
    audio.setNumChannels(channels);
    audio.setSampleRate(sampleRate);
    if (channels == 2) {
        for (auto& c : chunks) {
            for (size_t i = 0; i + 1 < c.second.size(); i += 2) {
                audio.samples[0].push_back(c.second[i]);
                audio.samples[1].push_back(c.second[i + 1]);
            }
        }
    } else if (channels == 1) {
        for (auto& c : chunks) {
            for (auto s : c.second) audio.samples[0].push_back(s);
        }
    }
    audio.save(path);
}

static void writeDng(const std::string& path, const std::vector<uint16_t>& data,
                     const nlohmann::json& meta,
                     const nlohmann::json& containerMeta) {
    unsigned width = meta["width"];
    unsigned height = meta["height"];
    std::vector<float> asShotNeutral = meta["asShotNeutral"];
    std::vector<uint16_t> blackLevel = containerMeta["blackLevel"];
    double whiteLevel = containerMeta["whiteLevel"];
    std::string sensorArrangement = containerMeta["sensorArrangment"];
    std::vector<float> colorMatrix1 = containerMeta["colorMatrix1"];
    std::vector<float> colorMatrix2 = containerMeta["colorMatrix2"];
    std::vector<float> forwardMatrix1 = containerMeta["forwardMatrix1"];
    std::vector<float> forwardMatrix2 = containerMeta["forwardMatrix2"];

    tinydngwriter::DNGImage dng;
    dng.SetBigEndian(false);
    dng.SetDNGVersion(1, 4, 0, 0);
    dng.SetDNGBackwardVersion(1, 1, 0, 0);
    dng.SetImageData(reinterpret_cast<const unsigned char*>(data.data()), data.size() * sizeof(uint16_t));
    dng.SetImageWidth(width);
    dng.SetImageLength(height);
    dng.SetPlanarConfig(tinydngwriter::PLANARCONFIG_CONTIG);
    dng.SetPhotometric(tinydngwriter::PHOTOMETRIC_CFA);
    dng.SetRowsPerStrip(height);
    dng.SetSamplesPerPixel(1);
    dng.SetCFARepeatPatternDim(2, 2);
    dng.SetBlackLevelRepeatDim(2, 2);
    dng.SetBlackLevel(4, blackLevel.data());
    dng.SetWhiteLevel(whiteLevel);
    dng.SetCompression(tinydngwriter::COMPRESSION_NONE);

    std::vector<uint8_t> cfa;
    if (sensorArrangement == "rggb") cfa = {0,1,1,2};
    else if (sensorArrangement == "bggr") cfa = {2,1,1,0};
    else if (sensorArrangement == "grbg") cfa = {1,0,2,1};
    else if (sensorArrangement == "gbrg") cfa = {1,2,0,1};
    else throw std::runtime_error("Invalid sensor arrangement");
    dng.SetCFAPattern(4, cfa.data());
    dng.SetCFALayout(1);
    const uint16_t bps[1] = {16};
    dng.SetBitsPerSample(1, bps);
    dng.SetColorMatrix1(3, colorMatrix1.data());
    dng.SetColorMatrix2(3, colorMatrix2.data());
    dng.SetForwardMatrix1(3, forwardMatrix1.data());
    dng.SetForwardMatrix2(3, forwardMatrix2.data());
    dng.SetAsShotNeutral(3, asShotNeutral.data());
    dng.SetCalibrationIlluminant1(21);
    dng.SetCalibrationIlluminant2(17);
    dng.SetUniqueCameraModel("MotionCam");
    dng.SetSubfileType();
    uint32_t area[4] = {0,0,height,width};
    dng.SetActiveArea(area);

    std::string err;
    tinydngwriter::DNGWriter writer(false);
    writer.AddImage(&dng);
    writer.WriteToFile(path.c_str(), &err);
}

static int convertFile(const std::string& input, const std::string& codec, std::string& status) {
    fs::path output = fs::path(input).replace_extension(codec == "prores" ? ".mov" : (codec == "dnxhr" ? ".mxf" : ".mp4"));
    try {
        motioncam::Decoder dec(input);
        auto frames = dec.getFrames();
        if (frames.empty()) { status = "No frames"; return -1; }
        auto containerMeta = dec.getContainerMetadata();
        double frameDurationNs = 0.0;
        if (frames.size() > 1) frameDurationNs = static_cast<double>(frames[1] - frames[0]);
        else frameDurationNs = 41708333.0;
        double fps = 1e9 / frameDurationNs;
        fs::path tempDir = fs::temp_directory_path() / "motioncam_batcher";
        fs::create_directories(tempDir);
        fs::path framePattern = tempDir / "frame_%06d.dng";
        fs::path audioPath = tempDir / "audio.wav";
        std::vector<motioncam::AudioChunk> audioChunks;
        dec.loadAudio(audioChunks);
        writeAudio(audioPath.string(), dec.audioSampleRateHz(), dec.numAudioChannels(), audioChunks);
        std::vector<uint8_t> rawBytes;
        std::vector<uint16_t> raw;
        nlohmann::json meta;
        char fname[32];
        for (size_t i = 0; i < frames.size(); ++i) {
            dec.loadFrame(frames[i], rawBytes, meta);
            raw.resize(rawBytes.size() / 2);
            std::memcpy(raw.data(), rawBytes.data(), rawBytes.size());
            std::snprintf(fname, sizeof(fname), "frame_%06zu.dng", i);
            writeDng((tempDir / fname).string(), raw, meta, containerMeta);
        }
        std::ostringstream cmd;
        cmd << "ffmpeg -y -r " << fps << " -i " << framePattern.string() << " -i " << audioPath.string();
        if (codec == "prores") {
            cmd << " -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le";
        } else if (codec == "dnxhr") {
            cmd << " -c:v dnxhd -profile:v dnxhr_hqx -pix_fmt yuv422p10le";
        } else if (codec == "hevc") {
            cmd << " -c:v hevc_amf -profile:v main10 -pix_fmt p010le";
        }
        cmd << " -c:a pcm_s16le \"" << output.string() << "\"";
        int ret = std::system(cmd.str().c_str());
        fs::remove_all(tempDir);
        if (ret != 0) { status = "ffmpeg failed"; return ret; }
        status = "Saved: " + output.string();
        return 0;
    } catch (const std::exception& e) {
        status = e.what();
        return -1;
    }
}

struct Clip {
    std::string path;
    std::unique_ptr<motioncam::Decoder> decoder;
    std::vector<int64_t> frames;
    int previewIndex = 0;
    int previewWidth = 0;
    int previewHeight = 0;
    GLuint previewTex = 0;
};

static void loadClipPreview(Clip& clip) {
    if (!clip.decoder) return;
    if (clip.frames.empty()) return;
    std::vector<uint8_t> rawBytes;
    nlohmann::json meta;
    clip.decoder->loadFrame(clip.frames[clip.previewIndex], rawBytes, meta);
    clip.previewWidth = meta["width"];
    clip.previewHeight = meta["height"];
    std::vector<uint8_t> img(rawBytes.size()/2);
    for (size_t i=0;i<img.size();++i) img[i] = rawBytes[i*2]>>2; // crude 8bit
    if (!clip.previewTex) glGenTextures(1, &clip.previewTex);
    glBindTexture(GL_TEXTURE_2D, clip.previewTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RED,clip.previewWidth,clip.previewHeight,0,GL_RED,GL_UNSIGNED_BYTE,img.data());
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;

#if defined(IMGUI_IMPL_OPENGL_ES2)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    SDL_Window* window = SDL_CreateWindow("MotionCam Batcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    std::vector<Clip> clips;
    int selected = -1;
    int codecIdx = 0; // 0 prores,1 dnxhr,2 hevc
    bool running = true;
    bool converting = false;
    size_t current = 0;
    std::string status;
    int rangeStart = 0, rangeEnd = 0;
    bool optVignette = true;
    bool optDontClip = false;
    bool optCompress = true;
    bool optGpu = false;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_DROPFILE) {
                char* path = event.drop.file;
                Clip c; c.path = path;
                try {
                    c.decoder = std::make_unique<motioncam::Decoder>(c.path);
                    c.frames = c.decoder->getFrames();
                    loadClipPreview(c);
                } catch (const std::exception&) {}
                clips.push_back(std::move(c));
                SDL_free(path);
            }
        }

        if (converting && current < clips.size()) {
            std::string codec = codecIdx==0?"prores":codecIdx==1?"dnxhr":"hevc";
            convertFile(clips[current].path, codec, status);
            current++;
            if (current >= clips.size()) converting = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("File List");
        if (ImGui::Button("Clear")) { clips.clear(); selected = -1; }
        ImGui::SameLine();
        if (ImGui::Button("Remove") && selected>=0 && selected < (int)clips.size()) {
            clips.erase(clips.begin()+selected);
            selected = -1;
        }
        ImGui::Separator();
        for (size_t i=0;i<clips.size();++i) {
            char label[64];
            std::snprintf(label,sizeof(label),"%zu: %s",i,fs::path(clips[i].path).filename().string().c_str());
            if (ImGui::Selectable(label, selected==(int)i)) {
                selected=i; loadClipPreview(clips[i]);
                rangeStart = 0; rangeEnd = clips[i].frames.size()>0?clips[i].frames.size()-1:0;
            }
        }
        ImGui::End();

        ImGui::SameLine();

        ImGui::Begin("Preview");
        if (selected>=0 && selected < (int)clips.size()) {
            Clip& c = clips[selected];
            if (c.previewTex) ImGui::Image((ImTextureID)(intptr_t)c.previewTex, ImVec2(320,180));
            if (!c.frames.empty()) {
                if (ImGui::SliderInt("Preview Frame", &c.previewIndex, 0, (int)c.frames.size()-1)) {
                    loadClipPreview(c);
                }
                ImGui::Text("Frames: %zu", c.frames.size());
            } else {
                ImGui::Text("No frames");
            }
        } else {
            ImGui::Text("Drop files to begin");
        }
        ImGui::End();

        ImGui::SameLine();

        ImGui::Begin("Export Settings");
        ImGui::InputInt("From", &rangeStart);
        ImGui::InputInt("To", &rangeEnd);
        ImGui::Checkbox("Apply vignette correction", &optVignette);
        ImGui::Checkbox("Don't clip highlights", &optDontClip);
        ImGui::Checkbox("Compress intermediate frames", &optCompress);
        if (ImGui::Button("Create Preview")) {}
        ImGui::SameLine();
        if (ImGui::Button("Apply Settings To All")) {}
        ImGui::End();

        ImGui::SameLine();

        ImGui::Begin("Render Config");
        ImGui::RadioButton("ProRes", &codecIdx, 0); ImGui::SameLine();
        ImGui::RadioButton("DNxHR", &codecIdx, 1); ImGui::SameLine();
        ImGui::RadioButton("HEVC", &codecIdx, 2);
        const char* transfers[] = {"SDR","Log","PQ"};
        static int transferIdx = 0;
        ImGui::Combo("Transfer Function", &transferIdx, transfers, IM_ARRAYSIZE(transfers));
        const char* fpsOpts[] = {"Original","24","30","60"};
        static int fpsIdx = 0;
        ImGui::Combo("Frame Rate", &fpsIdx, fpsOpts, IM_ARRAYSIZE(fpsOpts));
        ImGui::Checkbox("Enable GPU Rendering", &optGpu);
        if (!converting) {
            if (ImGui::Button("Start Rendering") && !clips.empty()) {
                converting = true; current = 0; status.clear();
            }
        } else {
            ImGui::Text("Processing %zu/%zu", current+1, clips.size());
        }
        if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        ImGui::End();

        ImGui::Separator();
        ImGui::Text("Add files to render\nSelect where to save them\nStart rendering");

        ImGui::Render();
        glViewport(0,0,(int)io.DisplaySize.x,(int)io.DisplaySize.y);
        glClearColor(0.1f,0.1f,0.1f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
