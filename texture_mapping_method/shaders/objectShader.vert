#version 330 core                       
layout (location = 0) in vec3 aPos;       // 頂點原始座標
layout (location = 1) in vec3 aNormal;    // 頂點法線
layout (location = 2) in vec3 aColor;     // 地形顏色
layout (location = 3) in vec3 aOffset;    // Instancing 用(用於植被批量生成)
layout (location = 4) in vec2 aTexCoords; // UV 座標

out vec3 FragPos;
out vec3 Normal;
out vec3 Color;
out vec2 TexCoords;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_plantScale;// 植被縮放控制

// [新增] UI 模式開關
uniform bool u_isUI;

void main() {
    if (u_isUI) {
        // UI 模式：直接輸出座標 (假設 aPos 已經是在螢幕座標範圍 -1~1 內)
        // 這裡我們利用 u_model 來控制 UI 的位置和大小
        gl_Position = u_model * vec4(aPos, 1.0);
        
        // 傳遞 UV 供貼圖使用
        TexCoords = aTexCoords;
        
        // UI 不需要法線和世界座標
        FragPos = vec3(0.0);
        Normal = vec3(0.0, 1.0, 0.0);
        Color = vec3(1.0);
    } 
    else {
        // --- 原本的地形/植被邏輯 ---
        vec3 scaledPos = aPos * u_plantScale;
        vec3 finalPos = scaledPos + aOffset;

        FragPos = vec3(u_model * vec4(finalPos, 1.0));
        Normal = mat3(transpose(inverse(u_model))) * aNormal;  
        Color = aColor;
        TexCoords = aTexCoords;
        
        gl_Position = u_projection * u_view * vec4(FragPos, 1.0);
    }
}