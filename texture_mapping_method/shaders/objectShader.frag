#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec3 Color;
in vec2 TexCoords;

struct Light {
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

uniform Light light;
uniform vec3 u_viewPos;
uniform float u_time;

// 貼圖 Sampler
uniform sampler2D sandTex;
uniform sampler2D grassTex;
uniform sampler2D gravelTex; // 碎石
uniform sampler2D mossTex;   // 苔癬
uniform sampler2D rockTex;
uniform sampler2D snowTex;

// [新增] 小地圖紋理 (我們借用一個沒用到的 slot，或者新增一個)
uniform sampler2D minimapTex;

// [新增] UI 控制
uniform bool u_isUI;
uniform bool u_useTexture; // 是顯示地圖(true) 還是玩家紅點(false)
uniform vec4 u_uiColor;    // 純色模式的顏色

uniform vec2 u_radarCenter;

// 控制變數
uniform bool u_isTerrain; 
uniform vec3 u_baseColor; // 用於植被顏色

uniform vec3 u_fogColor;    // 天空/霧氣顏色
uniform float u_fogDensity; // 霧氣濃度 (晚上可以濃一點)
uniform float u_sunIntensity; // 控制陽光強度 (晚上變弱)

vec3 calculateWaveNormal(vec2 uv, float time) {
    // 利用 sin/cos 隨時間偏移 uv 座標
    float wave1 = sin(uv.x * 8.0 + time * 1.2) * 0.4;
    float wave2 = cos(uv.y * 10.0 + time * 1.5) * 0.4;
    float wave3 = sin((uv.x + uv.y) * 5.0 + time * 0.8) * 0.2;
    
    // 組合出擾動的法線 (y 軸為主，x/z 為擾動)
    return normalize(vec3(wave1 + wave3, 1.0, wave2 + wave3));
}

uniform bool u_isFullMap;

void main() {
    // [新增] UI 渲染邏輯 (最優先處理)
    if (u_isUI) {
        if (u_useTexture) {
            vec2 sampleUV;
            float alpha = 0.85;

            if (u_isFullMap) {
                // --- 全地圖模式 (方形、靜態、顯示整張圖) ---
                sampleUV = vec2(TexCoords.x, 1.0 - TexCoords.y);
                alpha = 0.95;         // 不透明度高一點
            }
            else{
                // --- 小地圖模式 (圓形、雷達、跟隨玩家) ---
                 // 1. 計算圓形遮罩
                vec2 center = vec2(0.5, 0.5);
                float dist = distance(TexCoords, center);
                
                // 如果超出半徑 0.5，完全透明 (裁切成圓形)
                if (dist > 0.5) discard; 

                // 雷達捲動計算
                sampleUV = u_radarCenter + (TexCoords - 0.5) * 0.5;
                // 畫白框
                if (dist > 0.48) {
                    FragColor = vec4(1.0);
                    return;
                }
            }


            // 讀取高度並上色 (共用邏輯)
            float rawValue = texture(minimapTex, sampleUV).r;
            
            rawValue = max(0.0, rawValue - 0.08);

            // [重要] 還原真實世界高度，必須跟 main.cpp 一致
            float meshHeight = 160.0; 
            float worldH = pow(rawValue, 2.0) * meshHeight;
            
            vec3 mapColor;
            
            // 定義地形顏色 (與 3D 場景近似)
            vec3 cWater  = vec3(0.1, 0.3, 0.6); // 深藍
            vec3 cSand   = vec3(0.9, 0.8, 0.5); // 沙黃
            vec3 cGrass  = vec3(0.2, 0.5, 0.1); // 深綠
            vec3 cGravel = vec3(0.4, 0.35, 0.3);// 碎石褐
            vec3 cRock   = vec3(0.5, 0.5, 0.5); // 岩灰
            vec3 cSnow   = vec3(0.95, 0.95, 1.0); // 雪白

            // 根據高度決定顏色 (與 main.cpp 邏輯對應)
            if (worldH <= 11.2) {
                // 水面：做一點漸層
                mapColor = mix(vec3(0.1, 0.3, 0.5), cWater, rawValue);
            }
            else if (worldH < 12.0) mapColor = cSand;
            else if (worldH < 25.0) mapColor = mix(cSand, cGrass, (worldH - 12.0) / 13.0);
            else if (worldH < 40.0) mapColor = mix(cGrass, cGravel, (worldH - 25.0) / 15.0);
            else if (worldH < 55.0) mapColor = mix(cGravel, cRock, (worldH - 40.0) / 15.0);
            else if (worldH < 75.0) mapColor = mix(cRock, cSnow, (worldH - 55.0) / 20.0);
            else mapColor = cSnow;

            // 3. 增加立體感 (偽陰影)
            // 利用 texture 數值的變化率來估算坡度
            float dH = fwidth(rawValue) * 20.0; 
            mapColor -= vec3(dH); 

            FragColor = vec4(mapColor, alpha);

        } else {
            // --- 繪製玩家指標 (箭頭) ---
            // 這裡保持純色，由 C++ 控制旋轉
            FragColor = u_uiColor; 
        }
        return; // UI 畫完直接結束
    }

    vec2 uv = TexCoords * 8.0;

    //1. 取得貼圖顏色
    vec3 sand  = texture(sandTex, uv).rgb;
    vec3 grass = texture(grassTex, uv).rgb;
    vec3 gravel = texture(gravelTex, uv).rgb;
    vec3 moss  = texture(mossTex, uv).rgb;
    vec3 rock  = texture(rockTex, uv).rgb;
    vec3 snow  = texture(snowTex, uv).rgb;

    float h = FragPos.y; 
    vec3 objectColor;

    // 2. 決定物件顏色 (地形混合 or 純色)
    if (!u_isTerrain) {
        // 如果不是地形（是樹或花），直接使用傳入的顏色
        objectColor = u_baseColor;
    }
    else{
        // 水面渲染邏輯
        if (h <= 11.21) {
            // 1. 計算動態波浪法線
            vec3 waveNormal = calculateWaveNormal(TexCoords * 10.0, u_time);
            
            // 2. 菲涅耳效應 (Fresnel)：決定反射與透明度的比例
            vec3 viewDir = normalize(u_viewPos - FragPos);
            float fresnel = pow(1.0 - max(dot(viewDir, vec3(0.0, 1.0, 0.0)), 0.0), 4.0);
            fresnel = clamp(fresnel, 0.3, 0.8);

            // 3. 水面顏色與反射混合
            vec3 shallowColor = vec3(0.3, 0.7, 0.8); // 淺水青色
            vec3 skyColor = u_fogColor;              // 反射天空顏色
            vec3 waterBase = mix(shallowColor, vec3(0.05, 0.2, 0.4), clamp((11.2 - h) / 2.0, 0.0, 1.0));
            
            // 4. 高光計算 (Specular)：使用擾動後的法線
            vec3 lightDir = normalize(-light.direction);
            vec3 reflectDir = reflect(-lightDir, waveNormal); 
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 128.0);
            
            vec3 finalWater = mix(waterBase, skyColor, fresnel) + (light.specular * spec * 2.0);
            
            // 5. 霧氣計算
            float dist = length(u_viewPos - FragPos);
            float fogFactor = clamp(1.0 / exp(dist * u_fogDensity * dist * u_fogDensity), 0.0, 1.0);
            
            FragColor = vec4(mix(u_fogColor, finalWater, fogFactor), mix(0.5, 0.9, fresnel));
            return;
        }
        // 地形高度混合
        else if (h < 12.0) objectColor = sand;
        else if (h < 25.0) objectColor = mix(sand, grass, (h - 12.0) / 13.0);
        else if (h < 40.0) objectColor = mix(grass, gravel, (h - 25.0) / 15.0);
        else if (h < 55.0) objectColor = mix(gravel, moss, (h - 40.0) / 15.0);
        else if (h < 75.0) objectColor = mix(moss, rock, (h - 55.0) / 20.0);
        else objectColor = mix(rock, snow, clamp((h - 75.0) / 10.0, 0.0, 1.0));
    }


    // 3. 光照計算
    vec3 ambient = light.ambient * objectColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(norm, lightDir), 0.0); 
    vec3 diffuse = light.diffuse * diff * objectColor;

    vec3 viewDir = normalize(u_viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm); 

    float specPower = (h < 11.5) ? 64.0 : 32.0;
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), specPower);
    float specIntensity = (h < 11.5) ? 1.0 : 0.2;// 陸地反光較弱

    vec3 result = ambient + diffuse + (light.specular * spec * specIntensity * u_sunIntensity);

    //4. 霧氣混合
    float dist = length(u_viewPos - FragPos);
    float fogFactor = clamp(1.0 / exp(dist * u_fogDensity * dist * u_fogDensity), 0.0, 1.0);
    vec3 finalColor = mix(u_fogColor, result, fogFactor);

    FragColor = vec4(finalColor, 1.0);
}