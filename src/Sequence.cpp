#include <cstdlib>
#include <cstring>
#ifdef HAS_GLOB
#include <glob.h>
#endif
#include <algorithm>
#include <limits>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <sys/types.h> // stat
#include <sys/stat.h> // stat

#include "Sequence.hpp"
#include "Player.hpp"
#include "View.hpp"
#include "Colormap.hpp"
#include "Image.hpp"
#include "ImageProvider.hpp"
#include "ImageCollection.hpp"
#include "alphanum.hpp"
#include "globals.hpp"
#include "SVG.hpp"
#include "Histogram.hpp"
#include "editors.hpp"
#include "shaders.hpp"
#include "EditGUI.hpp"

Sequence::Sequence()
{
    static int id = 0;
    id++;
    ID = "Sequence " + std::to_string(id);

    view = nullptr;
    player = nullptr;
    colormap = nullptr;
    image = nullptr;
    imageprovider = nullptr;
    collection = nullptr;
    uneditedCollection= nullptr;
    editGUI = new EditGUI();

    valid = false;

    loadedFrame = -1;

    glob.reserve(2<<18);
    glob_.reserve(2<<18);
    glob = "";
    glob_ = "";
}

Sequence::~Sequence()
{
}

// from https://stackoverflow.com/a/236803
template<typename Out>
static void split(const std::string &s, char delim, Out result) {
    std::stringstream ss;
    ss.str(s);
    std::string item;
    std::string current;
    while (std::getline(ss, item, delim)) {
        // small hack to avoid splitting windows fullpath
        // we could use the result of:
        // unsigned int drive = GetDriveTypeA((item + ":").c_str());  // <fileapi.h>
        // but let's just assume that it is the drive if we are on windows
#ifdef WINDOWS
        if (item.size() == 1) {
            current = item;
        } else
#endif
        {
            if (!current.empty())
                item = current + ":" + item;
            *(result++) = item;
            current = "";
        }
    }
}

static bool is_file(const std::string& filename)
{
    struct stat info;
    return !stat(filename.c_str(), &info) && !(info.st_mode & S_IFDIR);  // let's assume any non-dir is a file
}

static bool is_directory(const std::string& filename)
{
    struct stat info;
    return !stat(filename.c_str(), &info) && (info.st_mode & S_IFDIR);
}


static void recursive_collect(std::vector<std::string>& filenames, std::string glob)
{
    // TODO: unit test all that
    std::vector<std::string> collected;

    bool found = false;
#ifdef HAS_GLOB
    glob_t res;
    ::glob(glob.c_str(), GLOB_TILDE | GLOB_NOSORT | GLOB_BRACE, NULL, &res);
    for(unsigned int j = 0; j < res.gl_pathc; j++) {
        std::string file(res.gl_pathv[j]);
        if (is_directory(file)) {
            std::string dirglob = file + (file[file.length()-1] != '/' ? "/*" : "*");
            recursive_collect(collected, dirglob);
        } else {
            collected.push_back(file);
        }
    }
    globfree(&res);
#else
    collected.push_back(glob.c_str());
    found = true;
#endif

    if (!strncmp(glob.c_str(), "/vsi", 4)) {
        filenames.push_back(glob);
    } else if (collected.empty()) {
        std::vector<std::string> substr;
        split(glob, ':', std::back_inserter(substr));
        if (substr.size() >= 2) {
            for (const std::string& s : substr) {
                recursive_collect(filenames, s);
            }
        } else {
            // this happens on Windows with 'C:' in filename
            // I don't know why these filenames are not captured by the globbing
            filenames.push_back(glob);
        }
    } else {
        std::sort(collected.begin(), collected.end(), doj::alphanum_less<std::string>());
        for (auto str : collected) {
            filenames.push_back(str);
        }
    }
}

void Sequence::loadFilenames() {
    std::vector<std::string> filenames;
    recursive_collect(filenames, std::string(glob.c_str()));

    if (filenames.empty() && !strcmp(glob.c_str(), "-")) {
        filenames.push_back("-");
    }

    ImageCollection* col = buildImageCollectionFromFilenames(filenames);
    this->collection = col;
    this->uneditedCollection = col;

    valid = filenames.size() > 0;
    strcpy(&glob_[0], &glob[0]);

    loadedFrame = -1;
    if (player)
        player->reconfigureBounds();

    svgcollection.resize(svgglobs.size());
    for (int j = 0; j < svgglobs.size(); j++) {
        if (!strcmp(svgglobs[j].c_str(), "auto")) {
            svgcollection[j] = filenames;
            for (int i = 0; i < svgcollection[j].size(); i++) {
                std::string filename = svgcollection[j][i];
                int h;
                for (h = filename.size()-1; h > 0 && filename[h] != '.'; h--)
                    ;
                filename.resize(h);
                filename = filename + ".svg";
                svgcollection[j][i] = filename;
            }
        } else {
            svgcollection[j].resize(0);
            recursive_collect(svgcollection[j], std::string(svgglobs[j].c_str()));
        }
    }

    forgetImage();
}

int Sequence::getDesiredFrameIndex() const
{
    assert(player);
    return std::min(player->frame, collection->getLength());
}

void Sequence::tick()
{
    bool shouldShowDifferentFrame = false;
    if (player && collection && loadedFrame != getDesiredFrameIndex()) {
        shouldShowDifferentFrame = true;
    }
    if (valid && shouldShowDifferentFrame && (image || !error.empty())) {
        forgetImage();
    }

    int desiredFrame = getDesiredFrameIndex();
    image = collection->getImage(desiredFrame - 1);
    loadedFrame = desiredFrame;
    error.clear();
    imageprovider = nullptr;
    //if (imageprovider && imageprovider->isLoaded()) {
        //ImageProvider::Result result = imageprovider->getResult();
        //if (result.has_value()) {
            //image = result.value();
            //error.clear();
            //LOG("new image: " << image);
        //} else {
            //error = result.error();
            //LOG("new error: " << error);
            //forgetImage();
        //}
        //gActive = std::max(gActive, 2);
        //imageprovider = nullptr;
        //if (image) {
            //auto mode = gSmoothHistogram ? Histogram::SMOOTH : Histogram::EXACT;
            //image->histogram->request(image, mode);
        //}
    //}

    if (image && colormap && !colormap->initialized) {
        colormap->autoCenterAndRadius(image->min, image->max);

        if (!colormap->shader) {
            switch (image->c) {
                case 1:
                    colormap->shader = getShader("gray");
                    break;
                case 2:
                    colormap->shader = getShader("opticalFlow");
                    break;
                default:
                case 4:
                case 3:
                    colormap->shader = getShader("default");
                    break;
            }
        }
        colormap->initialized = true;
    }
}

void Sequence::forgetImage()
{
#if 0
    LOG("forget image, was=" << image << " provider=" << imageprovider);
    image = nullptr;
    if (player && collection) {
        int desiredFrame = getDesiredFrameIndex();
        imageprovider = collection->getImageProvider(desiredFrame - 1);
        // /!\ a thread could have started the work here!
        AOIRequest req;
        // TODO: compute with respect to the view (and the window size and ratio...)
        req.ox = 3000;
        req.oy = 3000;
        req.w = 2000;
        req.h = 2000;
        //req.ox = 100;
        //req.oy = 100;
        //req.w = 250;
        //req.h = 250;
        req.bands.push_back(0);
        //req.bands.push_back(1);
        //req.bands.push_back(2);
        imageprovider->requestAOI(req);
        loadedFrame = desiredFrame;
    }
    LOG("forget image, new provider=" << imageprovider);
#endif
}

void Sequence::autoScaleAndBias(ImVec2 p1, ImVec2 p2, float quantile)
{
    std::shared_ptr<Image> img = getCurrentImage();
    if (!img)
        return;

    BandIndices bands = colormap->bands;
    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    bool norange = p1.x == p2.x && p1.y == p2.y && p1.x == 0 && p2.x == 0;

    if (!norange) {
        if (p1.x < 0) p1.x = 0;
        if (p1.y < 0) p1.y = 0;
        if (p2.x < 0) p2.x = 0;
        if (p2.y < 0) p2.y = 0;
        if (p1.x >= img->w - 1) p1.x = img->w - 1;
        if (p1.y >= img->h - 1) p1.y = img->h - 1;
        if (p2.x >= img->w) p2.x = img->w;
        if (p2.y >= img->h) p2.y = img->h;
        if (p1.x == p2.x)
            return;
        if (p1.y == p2.y)
            return;
    }

    if (quantile == 0) {
        if (norange) {
            low = img->min;
            high = img->max;
        } else {
            const float* data = (const float*) img->pixels;
            for (int d = 0; d < 3; d++) {
                int b = bands[d];
                if (b >= img->c)
                    continue;
                for (int y = p1.y; y < p2.y; y++) {
                    for (int x = p1.x; x < p2.x; x++) {
                        float v = data[b + img->c*(x+y*img->w)];
                        if (std::isfinite(v)) {
                            low = std::min(low, v);
                            high = std::max(high, v);
                        }
                    }
                }
            }
        }
    } else {
        std::vector<float> all;
        const float* data = (const float*) img->pixels;
        if (norange) {
            if (img->c <= 3 && bands == BANDS_DEFAULT) {
                // fast path
                all = std::vector<float>(data, data+img->w*img->h*img->c);
            } else {
                for (int d = 0; d < 3; d++) {
                    int b = bands[d];
                    if (b >= img->c)
                        continue;
                    for (int y = 0; y < img->h; y++) {
                        for (int x = 0; x < img->w; x++) {
                            float v = data[b + img->c*(x+y*img->w)];
                            all.push_back(v);
                        }
                    }
                }
            }
        } else {
            if (img->c <= 3 && bands == BANDS_DEFAULT) {
                // fast path
                for (int y = p1.y; y < p2.y; y++) {
                    const float* start = &data[0 + img->c*((int)p1.x+y*img->w)];
                    const float* end = &data[0 + img->c*((int)p2.x+y*img->w)];
                    all.insert(all.end(), start, end);
                }
            } else {
                for (int d = 0; d < 3; d++) {
                    int b = bands[d];
                    if (b >= img->c)
                        continue;
                    for (int y = p1.y; y < p2.y; y++) {
                        for (int x = p1.x; x < p2.x; x++) {
                            float v = data[b + img->c*(x+y*img->w)];
                            all.push_back(v);
                        }
                    }
                }
            }
        }
        all.erase(std::remove_if(all.begin(), all.end(),
                                 [](float x){return !std::isfinite(x);}),
                  all.end());
        std::sort(all.begin(), all.end());
        low = all[quantile*all.size()];
        high = all[(1-quantile)*all.size()];
    }

    colormap->autoCenterAndRadius(low, high);
}

void Sequence::snapScaleAndBias()
{
    std::shared_ptr<Image> img = getCurrentImage();
    if (!img)
        return;

    double min = img->min;
    double max = img->max;

    double dynamics[] = {1., std::pow(2, 8)-1, std::pow(2, 16)-1, std::pow(2, 32)-1};
    int best = 0;

    for (int d = sizeof(dynamics)/sizeof(double) - 1; d >= 0; d--) {
        if (min > -dynamics[d]/1.5 && max - min < dynamics[d]*2.)
            best = d;
    }

    colormap->autoCenterAndRadius(0., dynamics[best]);
}

std::shared_ptr<Image> Sequence::getCurrentImage() {
    return image;
}

float Sequence::getViewRescaleFactor() const
{
    if (!this->view->shouldRescale) {
        return 1.;
    }

    if (!this->view || !this->image) {
        return previousFactor;
    }

    size_t largestW = image->w;
    for (auto& seq : gSequences) {
        if (view == seq->view && seq->image && largestW < seq->image->w) {
            largestW = seq->image->w;
        }
    }
    previousFactor = (float) largestW / image->w;
    return previousFactor;
}

std::vector<const SVG*> Sequence::getCurrentSVGs() const
{
    std::vector<const SVG*> svgs;
    if (!player) goto end;
    for (auto& svgfilenames : svgcollection) {
        if (svgfilenames.empty()) {
            continue;
        }
        int frame = player->frame - 1;
        if (player->frame > svgfilenames.size()) {
            frame = 0;
        }
        svgs.push_back(SVG::get(svgfilenames[frame]));
    }
    for (auto& i : scriptSVGs) {
        svgs.push_back(i.second.get());
    }
end:
    return svgs;
}

const std::string Sequence::getTitle(int ncharname) const
{
    std::string seqname = std::string(glob.c_str());
    if (!valid)
        return "(the sequence '" + seqname + "' contains no images)";
    if (!player)
        return "(no player associated with the sequence '" + seqname + "')";
    if (!colormap)
        return "(no colormap associated with the sequence '" + seqname + "')";

    std::string title;
    int id = 0;
    while (gSequences[id] != this && id < gSequences.size())
        id++;
    id++;
    title += "#" + std::to_string(id) + " ";
    title += "[" + std::to_string(loadedFrame) + '/' + std::to_string(collection->getLength()) + "]";

    assert(loadedFrame);
    std::string filename(collection->getFilename(loadedFrame - 1));
    int p = filename.size() - ncharname;
    if (p < 0 || ncharname == -1) p = 0;
    if (p < filename.size()) {
        title += " " + filename.substr(p);
    }

    if (!image) {
        if (imageprovider) {
            title += " is loading";
        } else {
            title += " cannot be loaded";
        }
    }
    return title;
}

void Sequence::showInfo() const
{
    if (!valid || !player || !colormap)
        return;

    std::string seqname = std::string(glob.c_str());

    if (image) {
        int i = 0;
        for (auto svg : getCurrentSVGs()) {
            ImGui::Text("SVG %d: %s%s", i+1, svg->filename.c_str(), (!svg->valid ? " invalid" : ""));
            i++;
        }
        ImGui::Text("Size: %lux%lux%lu", image->w, image->h, image->c);
        ImGui::Text("Range: %g..%g", image->min, image->max);
        ImGui::Text("Zoom: %d%%", (int)(view->zoom * getViewRescaleFactor() * 100));
        ImGui::Separator();

        if (colormap->initialized) {
            float cmin, cmax;
            colormap->getRange(cmin, cmax, image->c);
            ImGui::Text("Displayed: %g..%g", cmin, cmax);
            ImGui::Text("Shader: %s", colormap->getShaderName().c_str());
        } else {
            ImGui::Text("Colormap not initialized");
        }
        if (editGUI->isEditing()) {
            ImGui::Text("Edited with %s", editGUI->getEditorName().c_str());
        }
    }
}

void Sequence::setEdit(const std::string& edit, EditType edittype)
{
    editGUI->edittype = edittype;
    strncpy(editGUI->editprog, edit.c_str(), sizeof(editGUI->editprog));
    editGUI->validate(*this);
}

std::string Sequence::getEdit()
{
    return editGUI->editprog;
}

int Sequence::getId()
{
    int id = 0;
    while (gSequences[id] != this && id < gSequences.size())
        id++;
    id++;
    return id;
}

std::string Sequence::getGlob() const
{
    return std::string(&glob[0]);
}

void Sequence::setGlob(const std::string& g)
{
    strncpy(&glob[0], &g[0], glob.capacity());
}

void Sequence::removeCurrentFrame()
{
    if (collection->getLength() <= 1) {
        return;
    }
    int index = player->frame - 1;
    collection = new MaskedImageCollection(uneditedCollection, index);
    uneditedCollection = collection;
    editGUI->validate(*this);
    player->reconfigureBounds();
    // TODO: handle SVG collection
}

bool Sequence::putScriptSVG(const std::string& key, const std::string& buf)
{
    if (buf.empty()) {
        scriptSVGs.erase(key);
    } else {
        std::shared_ptr<SVG> svg = SVG::createFromString(buf);
        if (svg->valid) {
            scriptSVGs[key] = svg;
        } else {
            return false;
        }
    }
    return true;
}
