#include <spdlog/spdlog.h>
#include <curl/curl.h>
#include <string>

using namespace std;

namespace netutils {
// private
static void libcurlInit()
{
    static bool inited = false;
    if(inited == false) {
        curl_global_init(CURL_GLOBAL_ALL);
        inited = true;
    }
}

//private
static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
    return written;
}
// https://curl.haxx.se/libcurl/c/url2file.html
int downloadFile(string fileUrl, string outUrl)
{
    CURL *curl;
    FILE *fp;
    libcurlInit();

    /* init the curl session */
    curl = curl_easy_init();

    /* set URL to get here */
    curl_easy_setopt(curl, CURLOPT_URL, fileUrl.c_str());

    /* Switch on full protocol/debug output while testing */
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);

    /* disable progress meter, set to 0L to enable and disable debug output */
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

    /* send all data to this function  */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

    /* open the file */
    fp = fopen(outUrl.c_str(), "wb");
    if(fp) {
        /* write the page body to this file handle */
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        auto res = curl_easy_perform(curl);
        if(res != CURLE_OK){
            spdlog::error("failed to upload files: {}", curl_easy_strerror(res));
            return -1;
        }

        /* close the header file */
        fclose(fp);
    }

    /* cleanup curl stuff */
    curl_easy_cleanup(curl);
    //curl_global_cleanup();

    return 0;
}
}

int main()
{
    netutils::downloadFile("https://curl.haxx.se/libcurl/c/url2file.html", "a.html");
}