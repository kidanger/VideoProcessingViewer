#include <string>

#include "Texture.hpp"
#include "Image.hpp"
#include "icons.hpp"

extern "C" {
#include "iio.h"
}

static Texture tex;


/*
# python3 > src/icons_tileset.c <<EOF
import numpy as np
import iio
u = iio.read('misc/icons.png').astype(np.uint8)
print('static int W='+str(u.shape[1])+', H='+str(u.shape[0])+', C='+str(u.shape[2])+';')
print('static unsigned char tileset[] = {' + ','.join(str(c) for c in u.flatten()) + '};')
EOF
*/
#include "icons_tileset.c"

static void load(void)
{
    std::shared_ptr<Image> image = std::make_shared<Image>(W, H, C);

    for (size_t d = 0; d < C; d++) {
        std::shared_ptr<Chunk> ck = std::make_shared<Chunk>();
        ck->w = W;
        ck->h = H;
        for (size_t i = 0; i < W*H; i++)
            ck->pixels[i] = (float) tileset[i*C+d] / 255.f;
        image->getBand(d)->setChunk(0, 0, ck);
    }

    tex.upload(image, ImRect(0, 0, image->w, image->h));
}

bool show_icon_button(IconID id, const char* description)
{
    static bool loaded = false;
    if (!loaded) {
        load();
        loaded = true;
    }
    ImTextureID icontex = (ImTextureID) (size_t) tex.tiles[0][0]->id;

    ImGui::PushID((std::string("button")+std::to_string(id)).c_str());
    float s = 16.f;
    ImVec2 uv0((id*(s+1))/W, 0.0);
    ImVec2 uv1((id*(s+1)+s)/W, s/H);
    ImVec4 background(0,0,0,0.5);
    bool clicked = ImGui::ImageButton(icontex, ImVec2(s, s), uv0, uv1, 2, background);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    return clicked;
}


