/*
  CMPT 276 with Dr. Ted Kirkpatrick (Spring 2016)

  Angel and the Saints
   - Josh Arik Miguel Fernandez
   - Angelina Singh
   - Woojin (Andrew) Song
   - Lawrence Yu

 Push Server code for CMPT 276, Spring 2016.
 */

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cpprest/base_uri.h>
#include <cpprest/http_listener.h>
#include <cpprest/json.h>

#include <pplx/pplxtasks.h>

#include <was/common.h>
#include <was/storage_account.h>
#include <was/table.h>

#include "TableCache.h"
#include "make_unique.h"
#include "ServerUtils.h"
#include "ClientUtils.h"

//#include "azure_keys.h"

using azure::storage::cloud_storage_account;
using azure::storage::storage_credentials;
using azure::storage::storage_exception;
using azure::storage::cloud_table;
using azure::storage::cloud_table_client;
using azure::storage::edm_type;
using azure::storage::entity_property;
using azure::storage::table_entity;
using azure::storage::table_operation;
using azure::storage::table_query;
using azure::storage::table_query_iterator;
using azure::storage::table_request_options;
using azure::storage::table_result;
using azure::storage::table_shared_access_policy;

using pplx::extensibility::critical_section_t;
using pplx::extensibility::scoped_critical_section_t;

using std::cin;
using std::cout;
using std::endl;
using std::getline;
using std::make_pair;
using std::pair;
using std::string;
using std::unordered_map;
using std::vector;

using web::http::http_headers;
using web::http::http_request;
using web::http::methods;
using web::http::status_code;
using web::http::status_codes;
using web::http::uri;

using web::json::value;

using web::http::experimental::listener::http_listener;

using prop_str_vals_t = vector<pair<string,string>>;
using prop_vals_t = vector<pair<string,value>>;

/////////////////////////////////////////////////////
//                                                 //
//                   Servers Used                  //
//                                                 //
/////////////////////////////////////////////////////

constexpr const char* basic_url = "http://localhost:34568/";
constexpr const char* auth_url = "http://localhost:34570/";
constexpr const char* user_url = "http://localhost:34572/";
constexpr const char* push_url = "http://localhost:34574/";

/////////////////////////////////////////////////////
//                                                 //
//                   Methods Used                  //
//                                                 //
/////////////////////////////////////////////////////

// For BasicServer
const string create_table {"CreateTableAdmin"};
const string delete_table {"DeleteTableAdmin"};
const string update_entity {"UpdateEntityAdmin"};
const string delete_entity {"DeleteEntityAdmin"};
const string update_property {"UpdatePropertyAdmin"};
const string add_property {"AddPropertyAdmin"};
const string read_entity {"ReadEntityAdmin"};
const string update_entity_auth{"UpdateEntityAuth"};
const string read_entity_auth{"ReadEntityAuth"};

// For AuthServer
const string auth_table_name {"AuthTable"};
const string auth_table_userid_partition {"Userid"};
const string auth_table_password_prop {"Password"};
const string auth_table_partition_prop {"DataPartition"};
const string auth_table_row_prop {"DataRow"};
const string data_table_name {"DataTable"};
const string get_read_token_op {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};
const string get_update_data {"GetUpdateData"};

// For UserServer
const string sign_on {"SignOn"};
const string sign_off {"SignOff"};
const string add_friend {"AddFriend"};
const string unfriend {"UnFriend"};
const string update_status {"UpdateStatus"};
const string read_friend_list {"ReadFriendList"};

const string friends {"Friends"};
const string status {"Status"};
const string updates {"Updates"};

// For PushServer
const string push_status {"PushStatus"};

/*

  Cache of opened tables
 */
//TableCache table_cache {};

/*
  Given an HTTP message with a JSON body, return the JSON
  body as an unordered map of strings to strings.

  Note that all types of JSON values are returned as strings.
  Use C++ conversion utilities to convert to numbers or dates
  as necessary.
 */
unordered_map<string,string> get_json_body(http_request message) {  
  unordered_map<string,string> results {};
  const http_headers& headers {message.headers()};
  auto content_type (headers.find("Content-Type"));
  if (content_type == headers.end() ||
      content_type->second != "application/json")
    return results;

  value json{};
  message.extract_json(true)
    .then([&json](value v) -> bool
          {
            json = v;
            return true;
          })
    .wait();

  if (json.is_object()) {
    for (const auto& v : json.as_object()) {
      if (v.second.is_string()) {
        results[v.first] = v.second.as_string();
      }
      else {
        results[v.first] = v.second.serialize();
      }
    }
  }
  return results;
}

/*
  Top-level routine for processing all HTTP GET requests.
 */
void handle_get(http_request message) { 
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PushServer GET " << path << endl;
}

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PushServer POST " << path << endl;
  auto paths = uri::split_path(path);
  
  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 3                        //
  // Start of Josh's code - Push a status update to all friends  //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // Need at least the method, partition, row, and status
  if(paths.size() < 4)
  {
      cout << "Paths has a size less than 4.\n";
      message.reply(status_codes::BadRequest);
      return;
  }

  if(paths[0] == push_status)
  {
      cout << "Inside Josh's code for POST PushStatus for all the friends of " << paths[2] << ".\n";

      string partition = paths[1];
      string row = paths[2];
      string status = paths[3];

      // Access all the user's friends
      unordered_map<string, string> properties = get_json_body(message);

      for(auto it = properties.begin(); it != properties.end(); ++it)
      {
        cout << "Property " << it->first << ": " << it->second << "\n"; 
      }

      cout << "All the friends: " << properties[friends] << endl;
      vector<pair<string,string>> actual_friends = parse_friends_list(properties[friends]);

      cout << "Number of friends this user has: " << actual_friends.size() << endl;

      for(int i = 0; i < actual_friends.size(); i++)
      {
        cout << "Friend " << actual_friends[i].first << ": " << actual_friends[i].second << "\n";
      }

      // Update all the user's "Update" statuses
      int final_result = 0;
      for(int i = 0; i < actual_friends.size(); i++)
      {

        // Get the friend's Update property
        pair<status_code, value> access_result
        {
          do_request (methods::GET, 
                      basic_url + read_entity + "/" + data_table_name + "/" + actual_friends[i].first + "/" + actual_friends[i].second)
        };
        cout << "Access properties result for " << actual_friends[i].second << ": " << access_result.first << endl;

        unordered_map<string, string> user_properties = unpack_json_object(access_result.second);

        // Append the friend's updates with the user's new status
        string initial_update = user_properties[updates];
        string new_update = initial_update + status + "\n";
        value updates_json { build_json_value(updates, new_update) };

        // Put it back in.
        pair<status_code, value> update_result
        {
          do_request (methods::PUT, 
                      basic_url + update_entity + "/" + data_table_name + "/" + actual_friends[i].first + "/" + actual_friends[i].second,
                      updates_json)
        };
        cout << "Update result for " << actual_friends[i].second << ": " << update_result.first << endl;

        // For convenience only
        final_result++;
      }

      // After all of these, signing in is finished and successful. Return status code "OK" and the update token
      if(final_result == actual_friends.size())
      {
          cout << "Pushing a status update was successful!\n";
          message.reply(status_codes::OK);
          return;
      }

      cout << "At the end of PushStatus block. Nothing was done.\n";
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 3                        //
  // Start of Josh's code - Push a status update to all friends  //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // If the message gave a malformed request, return a BadRequest
  message.reply(status_codes::BadRequest);
  return;
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PushServer PUT " << path << endl;
}

/*
  Top-level routine for processing all HTTP DELETE requests.
 */
void handle_delete(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PushServer DELETE " << path << endl;
}

/*
  Main user server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.

  USER Server only supports the POST method.
  If the you need other methods to get called, you can uncomment them.
  
  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {
  cout << "PushServer: Parsing connection string" << endl;
  //table_cache.init (storage_connection_string);

  cout << "PushServer: Opening listener" << endl;
  http_listener listener {push_url};
  //listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  //listener.support(methods::PUT, &handle_put);
  //listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop PushServer." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "PushServer closed" << endl;
}
