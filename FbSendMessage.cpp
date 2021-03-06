// FbSendMessage.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"


#include <stdio.h>
#include <curl/curl.h>
#include <iostream>
#include <cstring>
#include <vector>

#include "gumbo.h"

CURL* curl;
CURLcode res;

std::string data;
std::string fb_dtsg;
std::string own_id;

std::vector<std::string> friends;

struct curl_httppost* formpost = NULL;
struct curl_httppost* lastptr = NULL;
struct curl_httppost* msgform = NULL;
struct curl_httppost* msglast = NULL;

static size_t curl_write(void* ptr, size_t size, size_t nmemb, void* stream) {
	data.append((char*)ptr, size * nmemb);
	return size * nmemb;
};

std::string replace_all(std::string str, const std::string& from, const std::string& to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
	return str;
}

std::string string_between(std::string str, const std::string& from, const std::string& to) {
	size_t first = str.find(from);
	size_t last = str.find(to);
	return(str.substr(first + from.size(), last - first - to.size()));
}

int curl_check_cookie_response()
{
	struct curl_slist* cookies;
	struct curl_slist* nc;
	int i;
	res = curl_easy_getinfo(curl, CURLINFO_COOKIELIST, &cookies);
	if (res == CURLE_OK) {
		nc = cookies, i = 1;
		while (nc) {
			if (strstr(nc->data, "c_user") != NULL)
				return 0;
			nc = nc->next;
			i++;
		}
	}
	curl_slist_free_all(cookies);
	return 1;
}

int authenticate_details(const char* email, const char* password)
{
	curl_easy_setopt(curl, CURLOPT_URL, "https://m.facebook.com/login.php");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; titbang; Linux i686; rv:26.0) Gecko/20100101 Firefox/26.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 2L);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
	curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
	curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "cookies.txt");
	curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "email", CURLFORM_COPYCONTENTS, email, CURLFORM_END);
	curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "pass", CURLFORM_COPYCONTENTS, password, CURLFORM_END);
	curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
	if (curl_easy_perform(curl) == CURLE_OK) {
		return 0;
	}
	return 1;
}

void gumbo_parse_friend_data(GumboNode* node)
{
	GumboAttribute* url;
	if (node->type != GUMBO_NODE_ELEMENT) {
		return;
	}
	if (node->v.element.tag == GUMBO_TAG_A &&
		(url = gumbo_get_attribute(&node->v.element.attributes, "href"))) {
		if (strstr(url->value, "?uid=") != NULL) {
			data = string_between(url->value, "=", "&");
			data = replace_all(data, "=", "");
			friends.push_back(data);
		}
	}
	GumboVector* children = &node->v.element.children;
	for (unsigned int i = 0; i < children->length; ++i) {
		gumbo_parse_friend_data(static_cast<GumboNode*>(children->data[i]));
	}
}

void gumbo_parse_session_id(GumboNode* node)
{
	GumboAttribute* inputName; GumboAttribute* inputValue;
	if (node->type != GUMBO_NODE_ELEMENT) {
		return;
	}
	if (node->v.element.tag == GUMBO_TAG_INPUT) {
		inputName = gumbo_get_attribute(&node->v.element.attributes, "name");
		inputValue = gumbo_get_attribute(&node->v.element.attributes, "value");
		if (inputValue != NULL && inputName != NULL) {
			std::string val(inputName->value);
			std::size_t match = val.find("fb_dtsg");
			if (match == 0) {
				fb_dtsg = inputValue->value;
			}
		}
	}
	GumboVector* children = &node->v.element.children;
	for (unsigned int i = 0; i < children->length; ++i) {
		gumbo_parse_session_id(static_cast<GumboNode*>(children->data[i]));
	}
}

int grab_friends_list_data()
{
	curl_easy_setopt(curl, CURLOPT_URL, "https://m.facebook.com/friends/center/friends?_rdc=1&_rdr#_=_");
	if (curl_easy_perform(curl) == CURLE_OK) {
		GumboOutput* output = gumbo_parse(data.c_str());
		gumbo_parse_friend_data(output->root);
		return 0;
	}
	return 1;
}

int grab_friend_session(std::string friend_id)
{
	char url[512];
	snprintf(url, sizeof(url), "https://m.facebook.com/messages/read/?fbid=%s&_rdc=1&_rdr", friend_id.c_str());
	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (curl_easy_perform(curl) == CURLE_OK) {
		GumboOutput* output = gumbo_parse(data.c_str());
		gumbo_parse_session_id(output->root);
		return 0;
	}
	return 1;
}

int send_message_to_friend(std::string friend_id, std::string message)
{
	char field[32], value[32], tids[64];
	snprintf(field, sizeof(field), "ids[%s]", friend_id.c_str());
	snprintf(value, sizeof(value), "%s", friend_id.c_str());
	curl_easy_setopt(curl, CURLOPT_URL, "https://m.facebook.com/messages/send/?icm=1");
	curl_formadd(&msgform, &msglast, CURLFORM_COPYNAME, "fb_dtsg", CURLFORM_COPYCONTENTS, fb_dtsg.c_str(), CURLFORM_END);
	curl_formadd(&msgform, &msglast, CURLFORM_COPYNAME, field, CURLFORM_COPYCONTENTS, value, CURLFORM_END);
	curl_formadd(&msgform, &msglast, CURLFORM_COPYNAME, "body", CURLFORM_COPYCONTENTS, message.c_str(), CURLFORM_END);
	curl_easy_setopt(curl, CURLOPT_HTTPPOST, msgform);
	if (curl_easy_perform(curl) == CURLE_OK) {
		return 0;
	}
	return 1;
}

void cleanup() {
	data.clear();
}

int main(int argc, char* argv[]) {

	curl = curl_easy_init();

	if (curl) {

		if (authenticate_details("your facebook email", "your facebook password") == 0) {

			if (curl_check_cookie_response() == 0)
			{
				printf("We are logged in.\n");

				if (grab_friends_list_data() == 0)
				{
					for (std::vector<int>::size_type i = 0; i != friends.size(); i++)
					{
						printf("Sending message to friend ID: %s\r\n", friends[i].c_str());

						if (grab_friend_session(friends[i].c_str()) == 0) {
							send_message_to_friend(friends[i].c_str(), "hi");
						}
					}
				}
			}
			else {
				printf("Failed to login.");
			}
		}
	}

	return 0;
}


