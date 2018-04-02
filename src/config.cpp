#include <string>
#include <cassert>

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

#include "lua.hpp"
#include "kaguya/kaguya.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "Window.hpp"

// generated by cmake
extern "C" int load_luafiles(lua_State* L);

static lua_State* L;
static kaguya::State* state;

void config::load()
{
    L = luaL_newstate();
    assert(L);
    luaL_openlibs(L);

    state = new kaguya::State(L);

    (*state)["ImVec2"].setClass(kaguya::UserdataMetatable<ImVec2>()
                             .setConstructors<ImVec2(),ImVec2(float,float)>()
                             .addProperty("x", &ImVec2::x)
                             .addProperty("y", &ImVec2::y)
                             .addStaticFunction("__add", [](const ImVec2& a, const ImVec2& b) { return a+b; })
                            );

    (*state)["ImVec4"].setClass(kaguya::UserdataMetatable<ImVec4>()
                             .setConstructors<ImVec4()>()
                             .addProperty("x", &ImVec4::x)
                             .addProperty("y", &ImVec4::y)
                             .addProperty("z", &ImVec4::z)
                             .addProperty("w", &ImVec4::w)
                            );

    (*state)["ImRect"].setClass(kaguya::UserdataMetatable<ImRect>()
                             .setConstructors<ImRect(),ImRect(ImVec2,ImVec2)>()
                             .addProperty("min", &ImRect::Min)
                             .addProperty("max", &ImRect::Max)
                             .addFunction("get_width", &ImRect::GetWidth)
                             .addFunction("get_height", &ImRect::GetHeight)
                             .addFunction("get_size", &ImRect::GetSize)
                            );

    (*state)["Window"].setClass(kaguya::UserdataMetatable<Window>()
                             .setConstructors<Window()>()
                             .addProperty("opened", &Window::opened)
                             .addProperty("position", &Window::position)
                             .addProperty("size", &Window::size)
                             .addProperty("force_geometry", &Window::forceGeometry)
                             .addProperty("content_rect", &Window::contentRect)
                            );

    load_luafiles(L);
}

float config::get_float(const std::string& name)
{
#if 0
    lua_getglobal(L, name.c_str());
    float num = luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return num;
#else
    kaguya::State state(L);
    return state[name.c_str()];
#endif
}

bool config::get_bool(const std::string& name)
{
#if 0
    lua_getglobal(L, name.c_str());
    float num = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return num;
#else
    kaguya::State state(L);
    return state[name.c_str()];
#endif
}

std::string config::get_string(const std::string& name)
{
#if 0
    lua_getglobal(L, name.c_str());
    const char* str = luaL_checkstring(L, -1);
    std::string ret(str);
    lua_pop(L, 1);
    return ret;
#else
    kaguya::State state(L);
    return state[name.c_str()];
#endif
}

#include "shaders.hpp"
void config::load_shaders()
{
#if 0
    lua_getglobal(L, "SHADERS");
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        const char* name = lua_tostring(L, -2);
        const char* code = lua_tostring(L, -1);
        loadShader(name, code);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
#else
    kaguya::State state(L);
    std::map<std::string,std::string> shaders = state["SHADERS"];
    for (auto s : shaders) {
        loadShader(s.first, s.second);
    }
#endif
}

kaguya::State& config::get_lua()
{
    return *state;
}

