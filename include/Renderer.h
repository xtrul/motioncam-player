// --- START OF FILE Renderer.h ---
#ifndef RENDERER_H
#define RENDERER_H

#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#include <string>
#include <vector>
#include <array>
#include <nlohmann/json.hpp>
#include <cmath>
#include <optional>

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool initialize();
    void cleanup();
    void renderFrame(const std::vector<uint16_t>& rawData,
        const nlohmann::json& frameMetadata,
        double staticBlack,
        double staticWhite,
        int cfaType);

    static int getCfaType(const std::string& cfa);
    void setZoomNativePixels(bool nativePixels);
    void setPanOffsets(float x, float y);
    void resetPanOffsets();
    float getPanX() const;
    float getPanY() const;
    int getImageWidth()  const;
    int getImageHeight() const;

    // <<< --- ADD THIS LINE --- >>>
    void resetDimensions();
    // <<< --- END OF ADDITION --- >>>

private:
    static const char* computeShaderSrcOriginal;
    static const char* computeShaderSrcTiled;
    static const char* vertexShaderSrc;
    static const char* fragmentShaderSrc;

    static bool checkShader(GLuint sh, const std::string& type);
    static bool checkProgram(GLuint pr);
    static GLuint createShaderProgram(const char* vs, const char* fs);
    static GLuint createComputeProgram(const char* cs);
    static GLuint createQuadVAO();

    GLuint m_quadProg = 0;
    GLuint m_compProg = 0;
    GLuint m_quadVAO = 0;
    GLuint m_rawDataPBO = 0;
    GLuint m_rawDataTex = 0;
    GLuint m_outputDisplayTex = 0;

    int m_currentW = 0;
    int m_currentH = 0;
    bool m_zoomNativePixels = false;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
};

#endif // RENDERER_H
// --- END OF FILE Renderer.h ---