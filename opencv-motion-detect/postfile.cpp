#include "postfile.h"

namespace netutils{

static void libcurlInit(){
  static bool inited = false;
  if(inited == false) {
    curl_global_init(CURL_GLOBAL_ALL);
    inited = true;
  }
}

int postFiles(const char* url, vector<tuple<const char*, const char*> > params, vector<const char *> fileNames){
  CURL *curl;
  CURLcode res;
  curl_mime *form = NULL;
  curl_mimepart *field = NULL;
  struct curl_slist *headerlist = NULL;

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
    curl_mime_filedata(field, f);
  }

  string queryString;
  int cnt = 0;
  for(auto &[k, v]: params) {
    queryString += (cnt == 0?string(""):string("&")) +string(k) + "=" + string(v);
    cnt++;
  }

  string _url  = string(url) + string("?" ) + queryString;
  spdlog::info("_url: {}", _url);
  /* what URL that receives this POST */
  curl_easy_setopt(curl, CURLOPT_URL, _url.c_str());
  //curl_easy_setopt(curl, CURLOPT_POSTFIELDS, queryString.c_str());
  curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
  /* Perform the request, res will get the return code */
  res = curl_easy_perform(curl);
  /* Check for errors */
  if(res != CURLE_OK){
    spdlog::error("failed to upload files: {}", curl_easy_strerror(res));
    return -1;
  }

  /* always cleanup */
  curl_easy_cleanup(curl);
  /* then cleanup the form */
  curl_mime_free(form);
  /* free slist */
  curl_slist_free_all(headerlist);

  return 0;
}

}

