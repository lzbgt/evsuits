#include "postfile.h"

namespace netutils{
// private
static void libcurlInit(){
  static bool inited = false;
  if(inited == false) {
    curl_global_init(CURL_GLOBAL_ALL);
    inited = true;
  }
}

// private
static size_t WriteCallback(char *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int postFiles(string &&url, vector<tuple<string, string> > &&params, vector<string> &&fileNames, string &response){
  CURL *curl;
  CURLcode res;
  curl_mime *form = NULL;
  curl_mimepart *field = NULL;
  struct curl_slist *headerlist = NULL;
  int ret = 0;

  libcurlInit();
  curl = curl_easy_init();
  if(curl == NULL) {
    return -1;
  }

  /* Create the form */
  form = curl_mime_init(curl);

  /* Fill in the file upload field */
  for(auto &f: fileNames) {
    field = curl_mime_addpart(form);
    curl_mime_name(field, "files[]");
    curl_mime_filedata(field, f.c_str());
    spdlog::debug("curl file: {}", f);
  }

  string queryString;
  int cnt = 0;
  for(auto &[k, v]: params) {
    queryString += (cnt == 0?string(""):string("&")) + k + "=" + v;
    cnt++;
  }

  spdlog::debug("url is: {}, {}", url, url.c_str());

  string _url  = url + string("?" ) + queryString;
  spdlog::debug("_url: {}", _url);
  /* what URL that receives this POST */
  curl_easy_setopt(curl, CURLOPT_URL, _url.c_str());
  //curl_easy_setopt(curl, CURLOPT_POSTFIELDS, queryString.c_str());
  curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  /* Perform the request, res will get the return code */
  res = curl_easy_perform(curl);
  /* Check for errors */
  if(res != CURLE_OK){
    spdlog::error("failed to upload files: {}", curl_easy_strerror(res));
    switch(res){
      case CURLE_READ_ERROR:
      case CURLE_UPLOAD_FAILED:
      case CURLE_OPERATION_TIMEDOUT:
      case CURLE_SEND_ERROR:
      case CURLE_AGAIN:
      case CURLE_COULDNT_RESOLVE_PROXY:
      case CURLE_COULDNT_RESOLVE_HOST:
      case CURLE_COULDNT_CONNECT:
      case CURLE_WEIRD_SERVER_REPLY:
      case CURLE_REMOTE_ACCESS_DENIED:
      case CURLE_FAILED_INIT:
      //case CURLE_OUT_OF_MEMORY:
      case CURLE_HTTP_POST_ERROR:
      case CURLE_FILE_COULDNT_READ_FILE:{
        ret = 2;
        break;
      }
      default:
        ret = -1;
    }
  }

  /* always cleanup */
  curl_easy_cleanup(curl);
  /* then cleanup the form */
  curl_mime_free(form);
  /* free slist */
  curl_slist_free_all(headerlist);

  return ret;
}

}

