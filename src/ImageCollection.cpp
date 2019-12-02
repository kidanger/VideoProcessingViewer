#include <sys/stat.h>
#include "ImageProvider.hpp"
#include "Sequence.hpp"
#include "globals.hpp"
#include "watcher.hpp"
#include "ImageCollection.hpp"

#ifdef USE_GDAL
#include <gdal.h>
#endif

static std::shared_ptr<ImageProvider> selectProvider(const std::string& filename)
{
    struct stat st;
    unsigned char tag[4];
    FILE* file;

    if (gForceIioOpen) goto iio2;

    if (stat(filename.c_str(), &st) == -1 || S_ISFIFO(st.st_mode)) {
        // -1 can append because we use "-" to indicate stdin
        // all fifos are handled by iio
        goto iio;
    }

    file = fopen(filename.c_str(), "r");
    if (!file || fread(tag, 1, 4, file) != 4) {
        if (file) fclose(file);
        goto iio;
    }
    fclose(file);

    if (tag[0]==0xff && tag[1]==0xd8 && tag[2]==0xff) {
        return std::make_shared<JPEGFileImageProvider>(filename);
    } else if (tag[1]=='P' && tag[2]=='N' && tag[3]=='G') {
        return std::make_shared<PNGFileImageProvider>(filename);
    } else if ((tag[0]=='M' && tag[1]=='M') || (tag[0]=='I' && tag[1]=='I')) {
        // check whether the file can be opened with libraw or not
        if (RAWFileImageProvider::canOpen(filename)) {
            return std::make_shared<RAWFileImageProvider>(filename);
        } else {
            return std::make_shared<TIFFFileImageProvider>(filename);
        }
    }
iio:
#ifdef USE_GDAL
    {
        static int gdalinit = (GDALAllRegister(), 1);
        (void) gdalinit;
        // use OpenEX because Open outputs error messages to stderr
        GDALDatasetH* g = (GDALDatasetH*) GDALOpenEx(filename.c_str(),
                                                     GDAL_OF_READONLY | GDAL_OF_RASTER,
                                                     NULL, NULL, NULL);
        if (g) {
            GDALClose(g);
            return std::make_shared<GDALFileImageProvider>(filename);
        }
    }
#endif
iio2:
    return std::make_shared<IIOFileImageProvider>(filename);
}

std::shared_ptr<ImageProvider> SingleImageImageCollection::getImageProvider(int index) const
{
    std::string key = getKey(index);
    std::string filename = this->filename;
    auto provider = [key,filename]() {
        std::shared_ptr<ImageProvider> provider = selectProvider(filename);
        watcher_add_file(filename, [key](const std::string& fname) {
            LOG("file changed " << filename);
            ImageCache::Error::remove(key);
            ImageCache::remove(key);
            gReloadImages = true;
        });
        return provider;
    };
    return std::make_shared<CacheImageProvider>(key, provider);
}

std::shared_ptr<ImageProvider> EditedImageCollection::getImageProvider(int index) const
{
    std::string key = getKey(index);
    auto provider = [&]() {
        std::vector<std::shared_ptr<ImageProvider>> providers;
        for (auto c : collections) {
            providers.push_back(c->getImageProvider(index));
        }
        return std::make_shared<EditedImageProvider>(edittype, editprog, providers, key);
    };
    return std::make_shared<CacheImageProvider>(key, provider);
}

class VPPVideoImageProvider : public VideoImageProvider {
    FILE* file;
    int w, h, d;
    int curh;
    float* pixels;
public:
    VPPVideoImageProvider(const std::string& filename, int index, int w, int h, int d)
        : VideoImageProvider(filename, index),
          file(fopen(filename.c_str(), "r")), w(w), h(h), d(d), curh(0) {
        fseek(file, 4+3*sizeof(int)+w*h*d*sizeof(float)*index, SEEK_SET);
        pixels = (float*) malloc(w*h*d*sizeof(float));
    }

    ~VPPVideoImageProvider() {
        if (pixels)
            free(pixels);
        fclose(file);
    }

    float getProgressPercentage() const {
        return (float) curh / h;
    }

    void progress() {
        if (curh < h) {
            if (!fread(pixels+curh*w*d, sizeof(float), w*d, file)) {
                onFinish(makeError("error vpp"));
            }
            curh++;
        } else {
            auto image = std::make_shared<Image>(pixels, w, h, d);
            onFinish(image);
            pixels = nullptr;
        }
    }
};

class VPPVideoImageCollection : public VideoImageCollection {
    size_t length;
    int error;
    int w, h, d;
public:
    VPPVideoImageCollection(const std::string& filename) : VideoImageCollection(filename), length(0) {
        FILE* file = fopen(filename.c_str(), "r");
        char tag[4];
        if (fread(tag, 1, 4, file) == 4
            && fread(&w, sizeof(int), 1, file)
            && fread(&h, sizeof(int), 1, file)
            && fread(&d, sizeof(int), 1, file)) {
            fseek(file, 0, SEEK_END);
            length = (ftell(file)-4-3*sizeof(int)) / (w*h*d*sizeof(float));
        }
        fclose(file);
    }

    ~VPPVideoImageCollection() {
    }

    int getLength() const {
        return length;
    }

    std::shared_ptr<ImageProvider> getImageProvider(int index) const {
        auto provider = [&]() {
            return std::make_shared<VPPVideoImageProvider>(filename, index, w, h, d);
        };
        std::string key = getKey(index);
        return std::make_shared<CacheImageProvider>(key, provider);
    }
};

#include "cnpy.h"
class NumpyVideoImageProvider : public VideoImageProvider {
    int w, h, d;
    size_t length;
    cnpy::NpyArray array;
public:
    NumpyVideoImageProvider(const std::string& filename, int index, int w, int h,
                            int d, size_t length, cnpy::NpyArray array)
        : VideoImageProvider(filename, index), w(w), h(h), d(d), length(length), array(array) {
    }

    ~NumpyVideoImageProvider() {
    }

    float getProgressPercentage() const {
        return 1.f;
    }

    void progress() {
        float* pixels = (float*) malloc(sizeof(float) * w * h * d);
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                for (int c = 0; c < d; c++) {
                    size_t i = frame * w * h * d + c + d * (y * w + x);
                    size_t di = c + d * (y * w + x);
                    if (array.word_size == 4) {
                        pixels[di] = array.data<float>()[i];
                    } else {
                        pixels[di] = array.data<double>()[i];
                    }
                }
            }
        }
        auto image = std::make_shared<Image>(pixels, w, h, d);
        onFinish(image);
    }
};

class NumpyVideoImageCollection : public VideoImageCollection {
    size_t length;
    int w, h, d;
    cnpy::NpyArray array;

public:
    NumpyVideoImageCollection(const std::string& filename) : VideoImageCollection(filename), length(0) {
        array = cnpy::npy_load(filename);
        if (array.fortran_order) {
            fprintf(stderr, "numpy array '%s' is fortran order, please ask kidanger for support.\n",
                    filename.c_str());
            exit(1);
        }

        std::vector<size_t> shape = array.shape;
        d = 1;
        length = 1;
        if (shape.size() == 2) {
            h = shape[0];
            w = shape[1];
        } else if (shape.size() == 3) {
            if (shape[2] < shape[0] && shape[2] < shape[1]) {
                h = shape[0];
                w = shape[1];
                d = shape[2];
            } else {
                length = shape[0];
                h = shape[1];
                w = shape[2];
            }
        } else if (shape.size() == 4) {
            length = shape[0];
            h = shape[1];
            w = shape[2];
            d = shape[3];
        }
        const char* type = array.word_size == 4 ? "float" : "double";
        printf("opened numpy array '%s', assuming size: (n=%lu, h=%d, w=%d, d=%d), type=%s\n",
               filename.c_str(), length, h, w, d, type);
    }

    ~NumpyVideoImageCollection() {
    }

    int getLength() const {
        return length;
    }

    std::shared_ptr<ImageProvider> getImageProvider(int index) const {
        auto provider = [&]() {
            return std::make_shared<NumpyVideoImageProvider>(filename, index, w, h, d, length, array);
        };
        std::string key = getKey(index);
        return std::make_shared<CacheImageProvider>(key, provider);
    }
};

static ImageCollection* selectCollection(const std::string& filename)
{
    struct stat st;
    unsigned char tag[4];
    FILE* file;

    if (stat(filename.c_str(), &st) == -1 || S_ISFIFO(st.st_mode)) {
        goto end;
    }

    file = fopen(filename.c_str(), "r");
    if (!file || fread(tag, 1, 4, file) != 4) {
        if (file) fclose(file);
        goto end;
    }
    fclose(file);

    if (tag[0] == 'V' && tag[1] == 'P' && tag[2] == 'P' && tag[3] == 0) {
        return new VPPVideoImageCollection(filename);
    } else if (tag[0] == 0x93 && tag[1] == 'N' && tag[2] == 'U' && tag[3] == 'M') {
        return new NumpyVideoImageCollection(filename);
    }

end:
    return new SingleImageImageCollection(filename);
}


bool endswith(std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

ImageCollection* buildImageCollectionFromFilenames(std::vector<std::string>& filenames)
{
    if (filenames.size() == 1) {
        return selectCollection(filenames[0]);
    }

    //!\  here we assume that a sequence composed of multiple files means that each file contains only one image (not true for video files)
    MultipleImageCollection* collection = new MultipleImageCollection();
    for (auto& f : filenames) {
        if (endswith(f, ".npy")) {  // TODO: this is ugly, but faster than checking the tag
            collection->append(new NumpyVideoImageCollection(f));
        } else {
            collection->append(new SingleImageImageCollection(f));
        }
    }
    return collection;
}

