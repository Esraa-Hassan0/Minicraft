#version 330 core

#define MAX_LIGHTS 8

struct Light {
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    vec3 ambient;
    float attenConstant;
    float attenLinear;
    float attenQuadratic;
    float innerCutoff;
    float outerCutoff;
};

in Varyings {
    vec3 fragPos;
    vec3 normal;
    vec2 texcoord;
    vec4 fragPosLightSpace;
} fs_in;

uniform sampler2D albedoTex;
uniform sampler2D specularTex;
uniform sampler2D shadowMap;

uniform float shininess;
uniform vec3 cameraPos;

uniform int numLights;
uniform Light lights[MAX_LIGHTS];
uniform int enableShadows;
uniform vec3 shadowLightDir;

uniform vec3 flashColor;
uniform float flashStrength;

uniform vec4 tint;

out vec4 fragColor;

float calcDirectionalShadow(vec4 fragPosLightSpace, vec3 N, vec3 lightDir)
{
    vec3 projCoords = fragPosLightSpace.xyz / max(fragPosLightSpace.w, 0.0001);
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
    {
        return 0.0;
    }

    float bias = max(0.0015 * (1.0 - max(dot(N, lightDir), 0.0)), 0.0004);
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));

    float shadow = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(float(x), float(y)) * texelSize).r;
            shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

vec3 calcLight(Light L, vec3 N, vec3 V, vec3 albedo, vec3 spec, float directionalShadow)
{
    vec3 lightDir;
    float attenuation = 1.0;
    float shadowTerm = 0.0;

    if (L.type == 0) {
        lightDir = normalize(-L.direction);
        shadowTerm = directionalShadow;
    } else {
        vec3 delta = L.position - fs_in.fragPos;
        float dist = length(delta);
        lightDir = delta / dist;
        attenuation = 1.0 / (L.attenConstant
                            + L.attenLinear * dist
                            + L.attenQuadratic * dist * dist);

        if (L.type == 2) {
            vec3 spotDir = normalize(-L.direction);
            float theta = dot(lightDir, spotDir);
            float epsilon = L.innerCutoff - L.outerCutoff;
            attenuation *= clamp((theta - L.outerCutoff) / epsilon, 0.0, 1.0);
        }
    }

    vec3 H = normalize(lightDir + V);
    float diff = max(dot(N, lightDir), 0.0);
    float specular = pow(max(dot(N, H), 0.0), shininess);

    vec3 ambientC = L.ambient * albedo;
    vec3 diffuseC = L.color * diff * albedo * attenuation * (1.0 - shadowTerm);
    vec3 specularC = L.color * specular * spec * attenuation * (1.0 - shadowTerm);

    return ambientC + diffuseC + specularC;
}

void main() {
    vec4 albedoSample = texture(albedoTex, fs_in.texcoord) * tint;
    vec3 specSample = texture(specularTex, fs_in.texcoord).rgb;

    vec3 N = normalize(fs_in.normal);
    vec3 V = normalize(cameraPos - fs_in.fragPos);

    float directionalShadow = 0.0;
    if (enableShadows != 0)
    {
        vec3 lightDir = normalize(-shadowLightDir);
        directionalShadow = calcDirectionalShadow(fs_in.fragPosLightSpace, N, lightDir);
    }

    vec3 result = vec3(0.0);
    if (numLights > 0) {
        for (int i = 0; i < numLights && i < MAX_LIGHTS; i++) {
            float perLightShadow = 0.0;
            if (enableShadows != 0 && lights[i].type == 0)
            {
                float dirSimilarity = dot(normalize(lights[i].direction), normalize(shadowLightDir));
                if (dirSimilarity > 0.999)
                {
                    perLightShadow = directionalShadow;
                }
            }

            result += calcLight(lights[i], N, V, albedoSample.rgb, specSample, perLightShadow);
        }
    } else {
        result = albedoSample.rgb * vec3(0.05);
    }

    float ao = 1.0;
    if (N.y < 0.5) ao = 0.7;
    if (N.y < -0.5) ao = 0.4;
    result *= ao;

    if (albedoSample.a < 0.99) {
        vec3 deepWaterColor = vec3(0.05, 0.2, 0.5);
        result = mix(result, deepWaterColor, 0.6);
        albedoSample.a = max(albedoSample.a, 0.6);
    }

    result = mix(result, flashColor * albedoSample.rgb, flashStrength);

    if (albedoSample.a < 0.1) discard;

    fragColor = vec4(result, albedoSample.a);
}