/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2017, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
/* <DESC>
 * HTTP Multipart formpost with file upload and two additional parts.
 * </DESC>
 */
/* Example code that uploads a file name 'foo' to a remote script that accepts
 * "HTML form based" (as described in RFC1738) uploads using HTTP POST.
 *
 * The imaginary form we'll fill in looks like:
 *
 * <form method="post" enctype="multipart/form-data" action="examplepost.cgi">
 * Enter file: <input type="file" name="sendfile" size="40">
 * Enter file name: <input type="text" name="filename" size="30">
 * <input type="submit" value="send" name="submit">
 * </form>
 *
 * This exact source code has not been verified to work.
 */

#include <string>
#include <curl/curl.h>
#include <tuple>
#include <vector>
#include "inc/spdlog/spdlog.h"

using namespace std;

void libcurlInit(){
  static bool inited = false;
  if(inited == false) {
    curl_global_init(CURL_GLOBAL_ALL);
    inited = true;
  }
}

int postFiles(const char*url, vector<tuple<const char* const, const char* const> > params, vector<const char *> fileNames){
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

  string _url  = string(url) + "?" + queryString;
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

int main(int argc, char *argv[])
{
  vector<tuple<const char *const, const char *const> > params= {{"startTime", "1568027134"},{"endTime", "1568027254"},{"cameraId", "1"}, {"headOffset", "5"},{"tailOffset", "10"}};
  const char *url = "http://139.219.142.18:10008/upload/evtvideos/1";
  vector<const char*> fileNames = {"slices/1568027134.mp4", "slices/1568027254.mp4"};
  postFiles(url, params, fileNames);

  return 0;
}
