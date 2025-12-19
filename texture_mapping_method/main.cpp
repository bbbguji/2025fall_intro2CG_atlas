#include <cmath>
#include <string>
#include <random>
#include <iostream>
#include <vector>
#include <algorithm>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "lib/tiny_obj_loader.h"

#include "shader.h"
#include "camera.h"


// --- 全域設定 ---
const GLint WIDTH = 1920, HEIGHT = 1080;
float meshHeight = 160.0f;       // 地形高度
float WATER_HEIGHT = 11.2f;      // 水面高度
int chunk_render_distance = 8;  // 視距 (因為有優化，可以開遠一點)
int xMapChunks = 20;
int yMapChunks = 20;
int chunkWidth = 127;
int chunkHeight = 127;

// 世界原點偏移 (讓地圖置中)
float originX = (chunkWidth  * xMapChunks) / 2.0f - chunkWidth / 2.0f;
float originY = (chunkHeight * yMapChunks) / 2.0f - chunkHeight / 2.0f;

float MODEL_SCALE = 3.0f; // 植被縮放大小

// --- 小地圖相關變數 ---
GLuint minimapVAO = 0, minimapVBO = 0;
GLuint minimapTexture = 0;
// 地圖邊界 (用於計算玩家比例位置)
float mapMinX, mapMaxX, mapMinZ, mapMaxZ;
bool showFullMap = false;      // 是否顯示大地圖
bool mKeyPressed = false;      // 按鍵防彈跳用

// --- 晝夜系統全域變數 ---
enum class TimeOfDay { DAY = 0, DUSK = 1, NIGHT = 2, DAWN = 3 };
TimeOfDay gTimeOfDay = TimeOfDay::DAY;

// 目前的天空顏色 (用於 glClearColor)
glm::vec3 gSkyColor = glm::vec3(0.53f, 0.81f, 0.92f);

// --- 資源與狀態 ---
unsigned char* heightMapData = nullptr;
int hmWidth, hmHeight, hmChannels;
GLuint sandTex, grassTex, gravelTex, mossTex, rockTex, snowTex;

std::vector<int> treeInstanceCounts(xMapChunks * yMapChunks, 0);
std::vector<int> flowerInstanceCounts(xMapChunks * yMapChunks, 0);
int treeVCount = 0, flowerVCount = 0;

GLFWwindow *window;
Camera camera(glm::vec3(originX, 60.0f, originY));

// 滑鼠狀態
bool firstMouse = true;
float lastX = WIDTH / 2.0f, lastY = HEIGHT / 2.0f;
float deltaTime = 0.0f, lastFrame = 0.0f;
int nbFrames = 0;

struct plant {
    std::string type;
    float xpos, ypos, zpos;
    int xOffset, yOffset;
    plant(std::string _t, float _x, float _y, float _z, int _xo, int _yo) 
        : type(_t), xpos(_x), ypos(_y), zpos(_z), xOffset(_xo), yOffset(_yo) {}
};

// --- 函式宣告 ---
int init();
void processInput(GLFWwindow *window, Shader &shader);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void render(std::vector<GLuint> &map_chunks, Shader &shader, glm::mat4 &view, glm::mat4 &model, glm::mat4 &projection, int &nIndices, std::vector<GLuint> &tree_chunks, std::vector<GLuint> &flower_chunks, GLuint waterVAO, int waterIndices);
void load_heightmap_image(const char* path);
unsigned int loadTexture(const char* path);
int load_model(GLuint &VAO, std::string filename);
void setup_instancing(std::vector<GLuint> &plant_chunk, std::string plant_type, std::vector<plant> &plants, std::string filename, int &vCount);
void generate_map_chunk(GLuint &VAO, int xOffset, int yOffset, std::vector<plant> &plants);
void generate_water_chunk(GLuint &VAO, int &indexCount);

std::vector<int> generate_indices();
std::vector<float> generate_noise_map(int xOffset, int yOffset);
std::vector<float> generate_vertices(const std::vector<float> &noise_map);
std::vector<float> generate_normals(const std::vector<int> &indices, const std::vector<float> &vertices);
std::vector<float> generate_biome(const std::vector<float> &vertices, const std::vector<float> &normals, std::vector<plant> &plants, int xOffset, int yOffset);
void initMinimap();
void drawMinimap(Shader &shader);
void applyTimeOfDay(Shader &shader);
void drawFullMap(Shader &shader);

// --- 主程式 ---
int main() {
    srand(static_cast<unsigned int>(time(NULL)));
    if (init() != 0) return -1;

    // 1. 載入資源
    load_heightmap_image("./heightmap.png");
    initMinimap();
    Shader objectShader("shaders/objectShader.vert", "shaders/objectShader.frag");
    // 初始化光照 (取代原本寫死在 main 裡的 light 設定)
    applyTimeOfDay(objectShader);

    // 載入地形貼圖
    sandTex   = loadTexture("textures/sand.png");
    grassTex  = loadTexture("textures//grass.png");
    gravelTex = loadTexture("textures/mud.png");
    mossTex   = loadTexture("textures/moss.png");
    rockTex   = loadTexture("textures/rock.png");
    snowTex   = loadTexture("textures/snow.png");
    if(sandTex == 0) std::cout << "警告: sandTex 載入失敗！" << std::endl;

    // 2. 設定光照
    objectShader.use();
    objectShader.setVec3("light.ambient", 0.3f, 0.3f, 0.3f);
    objectShader.setVec3("light.diffuse", 0.8f, 0.8f, 0.75f);
    objectShader.setVec3("light.specular", 0.3f, 0.3f, 0.3f);
    objectShader.setVec3("light.direction", -0.2f, -1.0f, -0.3f);

    // 3. 生成地形
    std::cout << "Generating Terrain..." << std::endl;
    std::vector<GLuint> map_chunks(xMapChunks * yMapChunks);
    std::vector<plant> plants;
    
    for (int y = 0; y < yMapChunks; y++)
        for (int x = 0; x < xMapChunks; x++) {
            generate_map_chunk(map_chunks[x + y*xMapChunks], x, y, plants);
        }

    // 4. 生成植被 (Instancing)
    std::cout << "Generating Vegetation..." << std::endl;
    std::vector<GLuint> tree_chunks(xMapChunks * yMapChunks, 0);
    std::vector<GLuint> flower_chunks(xMapChunks * yMapChunks, 0);
       
    setup_instancing(tree_chunks, "tree", plants, "obj/CommonTree_1.obj", treeVCount);
    setup_instancing(flower_chunks, "flower", plants, "obj/Flowers.obj", flowerVCount);

    // 5. 生成水面
    GLuint waterVAO;
    int waterIndicesCount;
    generate_water_chunk(waterVAO, waterIndicesCount);

    int nIndices = chunkWidth * chunkHeight * 6;
    std::cout << "Initialization Complete." << std::endl;

    // --- Render Loop ---
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window, objectShader);
        glClearColor(gSkyColor.r, gSkyColor.g, gSkyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


        objectShader.use();
        objectShader.setBool("u_isUI", false);
        objectShader.setFloat("u_time", currentFrame);
        
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)WIDTH / (float)HEIGHT, 0.1f, 2000.0f);
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 model = glm::mat4(1.0f);

        objectShader.setMat4("u_projection", projection);
        objectShader.setMat4("u_view", view);
        objectShader.setVec3("u_viewPos", camera.Position);
        
        render(map_chunks, objectShader, view, model, projection, nIndices, tree_chunks, flower_chunks, waterVAO, waterIndicesCount);
        drawMinimap(objectShader);
        
        if (showFullMap) {
            drawFullMap(objectShader); // 按 M 顯示的大地圖
        }

        glfwPollEvents();
        glfwSwapBuffers(window);
    }
    
    if (heightMapData) stbi_image_free(heightMapData);
    glfwTerminate();
    return 0;
}

// --- 地形相關函式 ---
int get_mirrored_coord(int coord, int maxVal) {
    int cycle = 2 * maxVal;
    int val = std::abs(coord) % cycle;
    if (val >= maxVal) val = cycle - 1 - val;
    return val;
}

float get_smooth_height(int worldX, int worldY) {
    float total = 0.0f;
    int count = 0;
    for (int oy = -1; oy <= 1; oy++) {
        for (int ox = -1; ox <= 1; ox++) {
            int sx = get_mirrored_coord(worldX + ox, hmWidth);
            int sy = get_mirrored_coord(worldY + oy, hmHeight);
            total += heightMapData[sy * hmWidth + sx] / 255.0f;
            count++;
        }
    }
    return total / count;
}

std::vector<float> generate_noise_map(int xOffset, int yOffset) {
    std::vector<float> noiseValues;
    if (!heightMapData) {
        noiseValues.assign(chunkWidth * (chunkHeight + 1), 0.0f);
        return noiseValues;
    }
    for (int y = 0; y < chunkHeight + 1; y++) {
        for (int x = 0; x < chunkWidth; x++) {
            int wx = x + xOffset * (chunkWidth - 1);
            int wy = y + yOffset * (chunkHeight - 1);
            noiseValues.push_back(get_smooth_height(wx, wy));
        }
    }
    return noiseValues;
}

std::vector<float> generate_vertices(const std::vector<float> &noise_map) {
    std::vector<float> v;
    for (int y = 0; y < chunkHeight + 1; y++) {
        for (int x = 0; x < chunkWidth; x++) {
            v.push_back((float)x);
            // 高度非線性拉伸
            float rawVal = noise_map[x + y*chunkWidth];
            rawVal = std::max(0.0f, rawVal - 0.08f);
            float h = std::pow(rawVal, 2.0f) * meshHeight;
            v.push_back(h);
            v.push_back((float)y);
            // UV 座標
            v.push_back((float)x / (float)chunkWidth);
            v.push_back((float)y / (float)chunkHeight);
        }
    }
    return v;
}

// 生成植被邏輯
std::vector<float> generate_biome(const std::vector<float> &vertices, const std::vector<float> &normals, std::vector<plant> &plants, int xOffset, int yOffset) {
    std::vector<float> colors;

    for (int i = 0; i < vertices.size(); i += 5) { 
        float h = vertices[i + 1];
        float normalY = normals[(i/5)*3 + 1]; // 法線 Y 分量
        
        colors.push_back(1.0f); colors.push_back(1.0f); colors.push_back(1.0f);

        // 1. 高度 > 11.4: 高於水面
        // 2. h < 70.0: 低於林木線 (避免長在雪山上)
        // 3. normalY > 0.6: 僅在平緩處生長
        if (h > 11.4f && h < 70.0f && normalY > 0.6f) {

            // 機率控制 (目前約 0.5% 機率，可依需求微調)
            if ((rand() % 100000) < 15) { 
                std::string type = (rand() % 10 < 4) ? "tree" : "flower";
                plants.emplace_back(type, vertices[i], h, vertices[i+2], xOffset, yOffset);
            }
        }
    }
    return colors;
}

std::vector<float> generate_normals(const std::vector<int> &indices, const std::vector<float> &vertices) {
    std::vector<float> normals;
    std::vector<glm::vec3> tempNormals(vertices.size() / 5, glm::vec3(0.0f));
    
    for (int i = 0; i < indices.size(); i += 3) {
        int i1 = indices[i], i2 = indices[i+1], i3 = indices[i+2];
        glm::vec3 v1(vertices[i1*5], vertices[i1*5+1], vertices[i1*5+2]);
        glm::vec3 v2(vertices[i2*5], vertices[i2*5+1], vertices[i2*5+2]);
        glm::vec3 v3(vertices[i3*5], vertices[i3*5+1], vertices[i3*5+2]);
        
        // 計算面法線
        glm::vec3 norm = glm::cross(v2 - v1, v3 - v1);
        tempNormals[i1] += norm; tempNormals[i2] += norm; tempNormals[i3] += norm;
    }
    for (auto& n : tempNormals) {
        n = glm::normalize(n);
        normals.push_back(n.x); normals.push_back(n.y); normals.push_back(n.z);
    }
    return normals;
}

// 三角形索引順序 (Winding Order)
std::vector<int> generate_indices() {
    std::vector<int> indices;
    for (int y = 0; y < chunkHeight - 1; y++) {
        for (int x = 0; x < chunkWidth - 1; x++) {
            int pos = x + y * chunkWidth;
            
            // 修正為逆時針 (CCW) 順序，確保法線朝上
            // 三角形 1
            indices.push_back(pos);
            indices.push_back(pos + chunkWidth);
            indices.push_back(pos + chunkWidth + 1);
            
            // 三角形 2
            indices.push_back(pos);
            indices.push_back(pos + chunkWidth + 1);
            indices.push_back(pos + 1);
        }
    }
    return indices;
}

// --- 渲染相關 ---
bool is_chunk_visible(const glm::vec3& center, const glm::vec3& camPos, const glm::vec3& camFront, float radius) {
    float dist = glm::distance(glm::vec2(center.x, center.z), glm::vec2(camPos.x, camPos.z));
    if (dist > (chunk_render_distance * chunkWidth) * 1.5f) return false;
    glm::vec3 dirToChunk = center - camPos;
    if (glm::dot(camFront, dirToChunk) < -radius * 1.5f) return false;
    return true;
}

void render(std::vector<GLuint> &map_chunks, Shader &shader, glm::mat4 &view, glm::mat4 &model, glm::mat4 &projection, int &nIndices, std::vector<GLuint> &tree_chunks, std::vector<GLuint> &flower_chunks, GLuint waterVAO, int waterIndices) {
    //processInput(window, shader);

    // 背景色與霧氣顏色一致
    //glClearColor(gSkyColor.r, gSkyColor.g, gSkyColor.b, 1.0f);
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 綁定紋理
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sandTex);  shader.setInt("sandTex", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, grassTex); shader.setInt("grassTex", 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gravelTex);shader.setInt("gravelTex", 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, mossTex);  shader.setInt("mossTex", 3);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, rockTex);  shader.setInt("rockTex", 4);
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, snowTex);  shader.setInt("snowTex", 5);

    // 計算當前相機所在的區塊座標
    int gridPosX = (int)(camera.Position.x - originX) / chunkWidth + xMapChunks / 2;
    int gridPosY = (int)(camera.Position.z - originY) / chunkHeight + yMapChunks / 2;
    float chunkRadius = chunkWidth * 0.8f; 

    // --- Pass 1: 地形與植被 ---
    for (int y = 0; y < yMapChunks; y++) {
        for (int x = 0; x < xMapChunks; x++) {
            // 視距過濾
            if (std::abs(gridPosX - x) > chunk_render_distance || std::abs(y - gridPosY) > chunk_render_distance) continue;

            // 計算區塊中心點用於剔除檢查
            float cX = -chunkWidth / 2.0f + (chunkWidth - 1) * x + chunkWidth/2.0f;
            float cZ = -chunkHeight / 2.0f + (chunkHeight - 1) * y + chunkHeight/2.0f;
            if (!is_chunk_visible(glm::vec3(cX, 0, cZ), camera.Position, camera.Front, chunkRadius)) continue;

            int idx = x + y * xMapChunks;
            model = glm::translate(glm::mat4(1.0f), glm::vec3(-chunkWidth / 2.0f + (chunkWidth - 1) * x, 0.0f, -chunkHeight / 2.0f + (chunkHeight - 1) * y));
            shader.setMat4("u_model", model);
            
            // 繪製地形
            shader.setBool("u_isTerrain", true);
            glBindVertexArray(map_chunks[idx]);
            glDrawElements(GL_TRIANGLES, nIndices, GL_UNSIGNED_INT, 0);
            
            // 繪製樹木
            if (glIsVertexArray(tree_chunks[idx]) && treeInstanceCounts[idx] > 0) {
                // 切換為非地形模式，並設定顏色
                shader.setBool("u_isTerrain", false);
                shader.setVec3("u_baseColor", 0.1f, 0.35f, 0.1f); // 深綠色
                shader.setFloat("u_plantScale", MODEL_SCALE);
                glBindVertexArray(tree_chunks[idx]);
                glDrawArraysInstanced(GL_TRIANGLES, 0, treeVCount, treeInstanceCounts[idx]);
            }
            
            // 繪製花朵
            if (glIsVertexArray(flower_chunks[idx]) && flowerInstanceCounts[idx] > 0) {
                // 切換為非地形模式，並設定顏色
                shader.setBool("u_isTerrain", false);
                shader.setVec3("u_baseColor", 0.9f, 0.2f, 0.2f); // 紅色
                shader.setFloat("u_plantScale", MODEL_SCALE);
                glBindVertexArray(flower_chunks[idx]);
                glDrawArraysInstanced(GL_TRIANGLES, 0, flowerVCount, flowerInstanceCounts[idx]);
            }
            shader.setFloat("u_plantScale", 1.0f);
        }
    }

    // --- Pass 2: 半透明物體 (水面) --- 
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 深度偏移解決 Z-fighting
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    
    shader.setBool("u_isTerrain", true);
    shader.setMat4("u_model", glm::mat4(1.0f)); 
    glBindVertexArray(waterVAO);
    glDrawElements(GL_TRIANGLES, waterIndices, GL_UNSIGNED_INT, 0);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
}

void load_heightmap_image(const char* path) {
    heightMapData = stbi_load(path, &hmWidth, &hmHeight, &hmChannels, 1);
    if (heightMapData) std::cout << "[Info] Heightmap loaded: " << hmWidth << "x" << hmHeight << std::endl;
    else std::cout << "[Error] Failed to load heightmap: " << path << std::endl;
}

// --- 實作：模型載入 ---
int load_model(GLuint &VAO, std::string filename) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    std::string base_dir = filename.substr(0, filename.find_last_of("/\\") + 1);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), base_dir.c_str())) {
        std::cerr << "[Error] 無法載入模型: " << filename << " | " << err << std::endl;
        return 0;
    }

    std::vector<float> vertices;
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            // Position
            vertices.push_back(attrib.vertices[3 * index.vertex_index + 0]);
            vertices.push_back(attrib.vertices[3 * index.vertex_index + 1]);
            vertices.push_back(attrib.vertices[3 * index.vertex_index + 2]);
            // Normal
            vertices.push_back(attrib.normals[3 * index.normal_index + 0]);
            vertices.push_back(attrib.normals[3 * index.normal_index + 1]);
            vertices.push_back(attrib.normals[3 * index.normal_index + 2]);
            // Color (預設白色)
            vertices.push_back(1.0f); vertices.push_back(1.0f); vertices.push_back(1.0f);
        }
    }

    glGenVertexArrays(1, &VAO);
    GLuint VBO;
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // Layout (對應 objectShader.vert)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    return (int)(vertices.size() / 9); // 回傳正確頂點數
}

// --- 實作：水面生成 ---
void generate_water_chunk(GLuint &VAO, int &indexCount) {
    float startX = -chunkWidth / 2.0f;
    float startZ = -chunkHeight / 2.0f;
    float endX = startX + (chunkWidth - 1) * xMapChunks;
    float endZ = startZ + (chunkHeight - 1) * yMapChunks;

    float y = 11.2f; // 略低於沙灘分界線

    // 水面基礎色 (偏深藍)
    float wr=0.1f, wg=0.3f, wb=0.5f;

    // 2. 增加 UV 座標：目前的 Shader 預期 layout 4 有 UV，且能用於水波紋貼圖 
    // 頂點格式: x, y, z, nx, ny, nz, r, g, b, u, v (共 11 個 float)
    float vertices[] = {
        startX, y, startZ,  0.0f, 1.0f, 0.0f,  wr, wg, wb,  0.0f, 0.0f,
        endX,   y, startZ,  0.0f, 1.0f, 0.0f,  wr, wg, wb,  1.0f, 0.0f,
        endX,   y, endZ,    0.0f, 1.0f, 0.0f,  wr, wg, wb,  1.0f, 1.0f,
        startX, y, endZ,    0.0f, 1.0f, 0.0f,  wr, wg, wb,  0.0f, 1.0f
    };
    unsigned int indices[] = {0, 1, 2, 2, 3, 0}; 
    indexCount = 6;

    GLuint VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Layout 0: Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Layout 1: Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Layout 2: Color
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // Layout 4: TexCoords (UV)
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
}

// ... Instancing setup ...
void setup_instancing(std::vector<GLuint> &plant_chunk, std::string plant_type, std::vector<plant> &plants, std::string filename, int &vCount) {
    GLuint baseVAO;
    vCount = load_model(baseVAO, filename); 
    if (vCount == 0) {
        std::cout << "[Error] Failed to load model: " << filename << std::endl;
        return;
    }

    // 2. 準備各區塊的資料
    std::vector<std::vector<float>> chunkInstances(xMapChunks * yMapChunks);
    int totalPlants = 0;
    for (const auto& p : plants) {
        if (p.type == plant_type) {
            int idx = p.xOffset + p.yOffset * xMapChunks;
            // 這裡存入相對於區塊原點的座標
            chunkInstances[idx].push_back(p.xpos); 
            chunkInstances[idx].push_back(p.ypos);
            chunkInstances[idx].push_back(p.zpos);
            totalPlants++;
        }
    }
    std::cout << "[Debug] Type: " << plant_type << " Total Generated: " << totalPlants << std::endl;

    // 3. 為每個有植物的區塊配置 Instance Buffer
    for (int i = 0; i < xMapChunks * yMapChunks; i++) {
        if (chunkInstances[i].empty()) continue;

        if (plant_type == "tree") treeInstanceCounts[i] = chunkInstances[i].size() / 3;
        else flowerInstanceCounts[i] = chunkInstances[i].size() / 3;

        // 建立該區塊專用的 VAO，但共用模型 VBO
        glGenVertexArrays(1, &plant_chunk[i]);
        glBindVertexArray(plant_chunk[i]);

        // 重新綁定 load_model 生成的頂點 VBO (這裡為了簡單，我們重新 load 一次但僅限於該區塊)
        load_model(plant_chunk[i], filename); 

        // 增加 Offset VBO
        GLuint offsetVBO;
        glGenBuffers(1, &offsetVBO);
        glBindBuffer(GL_ARRAY_BUFFER, offsetVBO);
        glBufferData(GL_ARRAY_BUFFER, chunkInstances[i].size() * sizeof(float), chunkInstances[i].data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(3); // layout 3
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glVertexAttribDivisor(3, 1); // 關鍵：每繪製一個實例才更新一次屬性
    }
}

unsigned int loadTexture(const char* path) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    int width, height, nrComponents;
    unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format = (nrComponents == 4) ? GL_RGBA : GL_RGB;
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        stbi_image_free(data);
    }
    else {
        // 加入這一行幫助檢查路徑是否正確
        std::cout << "[Error] Cannot load texture: " << path << std::endl;
        std::cout << "stbi_failure_reason = "
              << stbi_failure_reason() << std::endl;
    }
    return textureID;
}

void generate_map_chunk(GLuint &VAO, int xOffset, int yOffset, std::vector<plant> &plants) {
    std::vector<int> indices = generate_indices();
    std::vector<float> noise_map = generate_noise_map(xOffset, yOffset);
    
    // 這裡調用修改後的 generate_vertices，它現在回傳 [x, y, z, u, v]
    std::vector<float> vertices = generate_vertices(noise_map); 
    std::vector<float> normals = generate_normals(indices, vertices);
    std::vector<float> colors = generate_biome(vertices, normals, plants, xOffset, yOffset);

    GLuint VBO[3], EBO; // 需要三個 VBO：一個給頂點+UV，一個給法線，一個給顏色
    glGenBuffers(3, VBO);
    glGenBuffers(1, &EBO);
    glGenVertexArrays(1, &VAO);
    
    glBindVertexArray(VAO);
    
    // VBO[0]: 位置 + UV
    glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(4);

    // VBO[1]: 法線
    glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
    glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(float), normals.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // VBO[2]: 顏色 (對應 layout location = 2)
    glBindBuffer(GL_ARRAY_BUFFER, VBO[2]);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(int), indices.data(), GL_STATIC_DRAW);
    
    glBindVertexArray(0);
}

void initMinimap() {
    glGenTextures(1, &minimapTexture);
    glBindTexture(GL_TEXTURE_2D, minimapTexture);
    
    if (heightMapData) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, hmWidth, hmHeight, 0, GL_RED, GL_UNSIGNED_BYTE, heightMapData);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    
    // 使用 MIRRORED_REPEAT 配合地形生成的邏輯
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // [修正] 恢復成標準的 Quad，不需要預先計算複雜的 UV
    // 我們在 Shader 裡動態計算位移
    float quadVertices[] = {
        // Positions        // Normals       // Colors        // Offset      // UV (標準 0~1)
        -0.5f,  0.5f, 0.0f, 0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f, 0.0f, // 左上
        -0.5f, -0.5f, 0.0f, 0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f, 1.0f, // 左下
         0.5f, -0.5f, 0.0f, 0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  1.0f, 1.0f, // 右下

        -0.5f,  0.5f, 0.0f, 0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f, 0.0f, // 左上
         0.5f, -0.5f, 0.0f, 0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  1.0f, 1.0f, // 右下
         0.5f,  0.5f, 0.0f, 0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  0.0f,0.0f,0.0f,  1.0f, 0.0f  // 右上
    };

    glGenVertexArrays(1, &minimapVAO);
    glGenBuffers(1, &minimapVBO);
    glBindVertexArray(minimapVAO);
    glBindBuffer(GL_ARRAY_BUFFER, minimapVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    int stride = 14 * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));

    glBindVertexArray(0);
}

void drawMinimap(Shader &shader) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader.use();
    shader.setBool("u_isUI", true); 

    // --- 1. 繪製地圖背景 (雷達模式) ---
    shader.setBool("u_useTexture", true);
    glActiveTexture(GL_TEXTURE6); 
    glBindTexture(GL_TEXTURE_2D, minimapTexture);
    shader.setInt("minimapTex", 6);

    // [關鍵] 計算玩家在圖片上的 UV 位置
    // 因為 generate_noise_map 是 1:1 對應 heightmap pixel
    // 所以直接除以圖片長寬即可
    float centerU = camera.Position.x / (float)hmWidth;
    float centerV = camera.Position.z / (float)hmHeight;
    shader.setVec2("u_radarCenter", glm::vec2(centerU, centerV));

    // 地圖位置與大小
    glm::vec3 mapCenterPos = glm::vec3(0.80f, 0.75f, 0.0f);
    float mapScale = 0.35f;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, mapCenterPos); 
    model = glm::scale(model, glm::vec3(mapScale, mapScale, 1.0f));     
    
    shader.setMat4("u_model", model);
    glBindVertexArray(minimapVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // --- 2. 繪製玩家箭頭 (固定在中心) ---
    shader.setBool("u_useTexture", false);
    shader.setVec4("u_uiColor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    // 箭頭直接畫在地圖中心 (因為地圖是隨著玩家動的)
    glm::mat4 pointModel = glm::mat4(1.0f);
    pointModel = glm::translate(pointModel, mapCenterPos); 
    
    // 旋轉箭頭 (跟隨相機 Yaw)
    pointModel = glm::rotate(pointModel, glm::radians(-camera.Yaw - 90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    pointModel = glm::scale(pointModel, glm::vec3(0.02f, 0.03f, 1.0f)); 
    
    shader.setMat4("u_model", pointModel);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    shader.setBool("u_isUI", false); 
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void applyTimeOfDay(Shader& sh) {
    glm::vec3 amb, dif, spc, fogCol;
    float fogDens, sunInt;

    switch (gTimeOfDay) {
    case TimeOfDay::DAY: // 白天
        fogCol  = glm::vec3(0.53f, 0.81f, 0.92f); // 藍天
        amb     = glm::vec3(0.3f, 0.3f, 0.3f);    // 環境光適中
        dif     = glm::vec3(0.8f, 0.8f, 0.75f);   // 陽光亮
        spc     = glm::vec3(0.3f, 0.3f, 0.3f);    // 反光正常
        fogDens = 0.0035f;                        // 視野清晰
        sunInt  = 1.0f;
        break;

    case TimeOfDay::DUSK: // 黃昏
        fogCol  = glm::vec3(0.8f, 0.5f, 0.3f);    // 橘紅天
        amb     = glm::vec3(0.3f, 0.2f, 0.2f);    // 環境光偏紅
        dif     = glm::vec3(0.6f, 0.4f, 0.3f);    // 陽光偏橘
        spc     = glm::vec3(0.2f, 0.2f, 0.1f);
        fogDens = 0.0045f;                        // 稍微朦朧
        sunInt  = 0.8f;
        break;

    case TimeOfDay::NIGHT: // 晚上
        fogCol  = glm::vec3(0.05f, 0.05f, 0.1f);  // 深藍黑
        amb     = glm::vec3(0.2f, 0.2f, 0.2f);  // 環境光極暗
        dif     = glm::vec3(0.1f, 0.1f, 0.15f);   // 月光微弱
        spc     = glm::vec3(0.1f, 0.1f, 0.1f);    // 幾乎無反光
        fogDens = 0.006f;                         // 視線變差
        sunInt  = 0.2f;
        break;
        
    case TimeOfDay::DAWN: // 清晨
        fogCol  = glm::vec3(0.6f, 0.6f, 0.7f);    // 霧白/紫
        amb     = glm::vec3(0.25f, 0.25f, 0.3f);
        dif     = glm::vec3(0.5f, 0.5f, 0.5f);
        spc     = glm::vec3(0.2f, 0.2f, 0.2f);
        fogDens = 0.005f;                         // 霧氣較重
        sunInt  = 0.6f;
        break;
    }

    // 更新全域天空色 (給 glClearColor 用)
    gSkyColor = fogCol;

    // 傳送給 Shader
    sh.use();
    sh.setVec3("u_fogColor", fogCol);
    sh.setFloat("u_fogDensity", fogDens);
    sh.setFloat("u_sunIntensity", sunInt);
    
    // 更新原本的光照 Struct
    sh.setVec3("light.ambient", amb);
    sh.setVec3("light.diffuse", dif);
    sh.setVec3("light.specular", spc);
    
    // 可選：改變光照方向 (例如黃昏太陽比較低)
    if (gTimeOfDay == TimeOfDay::NIGHT)
        sh.setVec3("light.direction", -0.2f, -1.0f, -0.3f); // 月光通常較高
    else if (gTimeOfDay == TimeOfDay::DUSK || gTimeOfDay == TimeOfDay::DAWN)
        sh.setVec3("light.direction", -0.8f, -0.3f, -0.3f); // 太陽在側面
    else
        sh.setVec3("light.direction", -0.2f, -1.0f, -0.3f); // 正午太陽
}

void drawFullMap(Shader &shader) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader.use();
    shader.setBool("u_isUI", true); 
    shader.setBool("u_isFullMap", true); // 告訴 Shader 這是全地圖

    // --- 1. 繪製地圖背景 (螢幕中央大方形) ---
    shader.setBool("u_useTexture", true);
    glActiveTexture(GL_TEXTURE6); 
    glBindTexture(GL_TEXTURE_2D, minimapTexture);
    shader.setInt("minimapTex", 6);

    // 螢幕置中，大小設為 1.5 (佔據大部分畫面)
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::scale(model, glm::vec3(1.5f, 1.5f, 1.0f)); 
    shader.setMat4("u_model", model);
    
    glBindVertexArray(minimapVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // --- 2. 繪製玩家位置 (紅點) ---
    shader.setBool("u_useTexture", false);
    shader.setVec4("u_uiColor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    // [計算玩家在單張地圖上的 UV]
    // 因為世界是無限鏡像拼接的，我們需要把玩家座標 "折疊" 回 0~1 的範圍
    float u = camera.Position.x / (float)hmWidth;
    float v = camera.Position.z / (float)hmHeight;

    // 處理 GL_MIRRORED_REPEAT 的邏輯
    // 偶數區塊 (0~1, 2~3...) 是正常，奇數區塊 (1~2, 3~4...) 是鏡像翻轉
    float cycle = 2.0f;
    float normU = std::abs(u); // 取絕對值
    float normV = std::abs(v);
    
    // 取餘數 (0~2)
    normU = fmod(normU, cycle);
    normV = fmod(normV, cycle);

    // 如果在 1~2 之間，要翻轉 (2 - val)
    if (normU > 1.0f) normU = 2.0f - normU;
    if (normV > 1.0f) normV = 2.0f - normV;

    // 現在 normU, normV 就是玩家在愛心圖案上的 0~1 位置
    
    // 將 0~1 轉換為螢幕座標 (Quad 是 -0.5 ~ 0.5，且放大了 1.5 倍)
    // 公式: (UV - 0.5) * Scale
    float playerScreenX = (normU - 0.5f) * 1.5f;
    float playerScreenY = (normV - 0.5f) * 1.5f; // 注意 V 軸方向

    // 繪製紅點
    glm::mat4 pointModel = glm::mat4(1.0f);
    pointModel = glm::translate(pointModel, glm::vec3(playerScreenX, playerScreenY, 0.0f));
    // 旋轉箭頭
    pointModel = glm::rotate(pointModel, glm::radians(-camera.Yaw - 90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    pointModel = glm::scale(pointModel, glm::vec3(0.03f, 0.05f, 1.0f)); 
    
    shader.setMat4("u_model", pointModel);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 恢復狀態
    shader.setBool("u_isUI", false); 
    shader.setBool("u_isFullMap", false); // 記得關掉，不然會影響小地圖
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

int init() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Terrain Fix", nullptr, nullptr);
    if (!window) return -1;
    glfwMakeContextCurrent(window);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glEnable(GL_DEPTH_TEST);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    return 0;
}
void processInput(GLFWwindow *window, Shader &shader) {
    float fastDelta = deltaTime * 5.0f;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) { shader.use(); shader.setBool("isFlat", false); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); }
    if (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS) { shader.use(); shader.setBool("isFlat", true); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); }
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, fastDelta);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, fastDelta);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, fastDelta);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, fastDelta);
    // [新增] 晝夜切換按鍵
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        gTimeOfDay = TimeOfDay::DAY;
        applyTimeOfDay(shader);
    }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        gTimeOfDay = TimeOfDay::DUSK;
        applyTimeOfDay(shader);
    }
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) {
        gTimeOfDay = TimeOfDay::NIGHT;
        applyTimeOfDay(shader);
    }
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
        gTimeOfDay = TimeOfDay::DAWN;
        applyTimeOfDay(shader);
    }
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
        if (!mKeyPressed) {
            showFullMap = !showFullMap;
            mKeyPressed = true;
        }
    } else {
        mKeyPressed = false;
    }
}
void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;
    camera.ProcessMouseMovement(xoffset, yoffset);
}
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) { camera.ProcessMouseScroll(yoffset); }


