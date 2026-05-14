#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <chrono>
#include <vector>
#include <cstring>

struct PremiereCamera {
    glm::vec3 pos{0.0f, 0.0f, 1000.0f};
    glm::vec3 rot{0.0f};
    float fov = 30.0f;
    bool autoPush = true;
    float pushSpeed = 50.0f;

    glm::mat4 getViewProj(int w, int h, float time) {
        if(autoPush) pos.z = 1000.0f - fmod(time * pushSpeed, 400.0f);
        glm::mat4 proj = glm::perspective(glm::radians(fov), float(w)/h, 1.0f, 10000.0f);
        glm::mat4 view = glm::translate(glm::mat4(1.0f), -pos);
        view = glm::rotate(view, glm::radians(rot.x), glm::vec3(1,0,0));
        view = glm::rotate(view, glm::radians(rot.y), glm::vec3(0,1,0));
        view = glm::rotate(view, glm::radians(rot.z), glm::vec3(0,0,1));
        return proj * view;
    }
};

struct PosTexVertex {
    float x, y, z;
    float u, v;
};

static PosTexVertex quadVertices[] = {
    {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
    { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
    {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
    { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
};
static const uint16_t quadIndices[] = {0,1,2, 1,3,2};

class VideoDecoder {
public:
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    SwsContext* sws = nullptr;
    int videoStream = -1;
    AVFrame* frame = nullptr;
    AVFrame* rgb = nullptr;
    int width=0, height=0;

    bool open(const char* path) {
        avformat_open_input(&fmt, path, nullptr, nullptr);
        avformat_find_stream_info(fmt, nullptr);
        for(unsigned i=0;i<fmt->nb_streams;i++){
            if(fmt->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
                videoStream=i; break;
            }
        }
        auto* dec = avcodec_find_decoder(fmt->streams[videoStream]->codecpar->codec_id);
        codec = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(codec, fmt->streams[videoStream]->codecpar);
        avcodec_open2(codec, dec, nullptr);
        width = codec->width; height = codec->height;
        frame = av_frame_alloc();
        rgb = av_frame_alloc();
        rgb->format = AV_PIX_FMT_RGBA;
        rgb->width = width; rgb->height = height;
        av_frame_get_buffer(rgb, 1);
        sws = sws_getContext(width,height,codec->pix_fmt,width,height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
        return true;
    }
    bool readFrame(uint8_t* out) {
        AVPacket pkt;
        while(av_read_frame(fmt,&pkt)>=0){
            if(pkt.stream_index==videoStream){
                avcodec_send_packet(codec,&pkt);
                if(avcodec_receive_frame(codec,frame)==0){
                    sws_scale(sws, frame->data, frame->linesize,0,height,rgb->data,rgb->linesize);
                    memcpy(out, rgb->data[0], width*height*4);
                    av_packet_unref(&pkt);
                    return true;
                }
            }
            av_packet_unref(&pkt);
        }
        av_seek_frame(fmt, videoStream, 0, AVSEEK_FLAG_BACKWARD);
        return false;
    }
};

int main(int argc, char** argv){
    if(argc<2){ std::cout<<"Uso: ./app video.mp4\n"; return 1;}

    glfwInit();
    GLFWwindow* win = glfwCreateWindow(1280,720,"Premiere Camera - bgfx",nullptr,nullptr);

    bgfx::Init init;
    init.platformData.nwh = (void*)glfwGetX11Window(win);
    init.type = bgfx::RendererType::OpenGL;
    init.resolution.width=1280; init.resolution.height=720;
    bgfx::init(init);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(win,true);

    VideoDecoder dec; dec.open(argv[1]);
    std::vector<uint8_t> videoBuffer(dec.width*dec.height*4);

    bgfx::TextureHandle videoTex = bgfx::createTexture2D(dec.width, dec.height, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE);

    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
          .add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
    bgfx::VertexBufferHandle vb = bgfx::createVertexBuffer(bgfx::makeRef(quadVertices,sizeof(quadVertices)), layout);
    bgfx::IndexBufferHandle ib = bgfx::createIndexBuffer(bgfx::makeRef(quadIndices,sizeof(quadIndices)));

    // NOTE: compila gli shader con shaderc, qui placeholder
    bgfx::ShaderHandle vsh = bgfx::createShader(bgfx::makeRef("",0));
    bgfx::ShaderHandle fsh = bgfx::createShader(bgfx::makeRef("",0));
    bgfx::ProgramHandle prog = bgfx::createProgram(vsh,fsh,true);
    bgfx::UniformHandle u_viewProj = bgfx::createUniform("u_viewProj", bgfx::UniformType::Mat4);
    bgfx::UniformHandle s_tex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

    PremiereCamera cam;
    auto start = std::chrono::high_resolution_clock::now();

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();
        auto now = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(now-start).count();

        ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        ImGui::Begin("Premiere Camera");
        ImGui::SliderFloat3("Pos", &cam.pos.x, -500,500);
        ImGui::SliderFloat("Pos Z", &cam.pos.z, 200,2000);
        ImGui::SliderFloat3("Rot", &cam.rot.x, -45,45);
        ImGui::SliderFloat("FOV", &cam.fov, 10,90);
        ImGui::Checkbox("Auto Push-in", &cam.autoPush);
        ImGui::End();

        dec.readFrame(videoBuffer.data());
        bgfx::updateTexture2D(videoTex,0,0,0,0,dec.width,dec.height, bgfx::makeRef(videoBuffer.data(), videoBuffer.size()));

        bgfx::touch(0);
        bgfx::setViewRect(0,0,0,1280,720);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x111111ff);

        glm::mat4 vp = cam.getViewProj(1280,720,time);
        glm::mat4 model = glm::scale(glm::mat4(1), glm::vec3(dec.width/2.0f, dec.height/2.0f,1));
        glm::mat4 mvp = vp * model;
        bgfx::setUniform(u_viewProj, &mvp);

        bgfx::setVertexBuffer(0,vb); bgfx::setIndexBuffer(ib);
        bgfx::setTexture(0,s_tex,videoTex);
        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
        bgfx::submit(0,prog);

        ImGui::Render();
        // qui andrebbe ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData());

        bgfx::frame();
    }

    bgfx::destroy(videoTex); bgfx::shutdown();
    glfwTerminate();
    return 0;
}
