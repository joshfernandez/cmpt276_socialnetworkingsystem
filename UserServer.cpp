/*
  CMPT 276 with Dr. Ted Kirkpatrick (Spring 2016)

  Angel and the Saints
   - Josh Arik Miguel Fernandez
   - Angelina Singh
   - Woojin (Andrew) Song
   - Lawrence Yu

 User Server code for CMPT 276, Spring 2016.
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
using std::tuple;
using std::make_tuple;
using std::get;
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

////////////////////////////////////////////////////////////////////////////
//                                                                        //
//                 The list of users with active sessions                 //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

unordered_map<string, tuple<string, string, string>> active_users {};

/*
  A function that adds the specified user to the list of active users.
  Used when signing in a user
*/
void add_user(const string& userid, const string& token, const string& data_partition, const string& data_row)
{
  cout << "Adding the user " << userid << endl;
  tuple<string, string, string> data_tuple {make_tuple(token, data_partition, data_row)};
  active_users.insert({userid, data_tuple});

  for(auto it = active_users.begin(); it != active_users.end(); ++it)
  {
    cout << "\tUser " << it->first << ": " << get<1>(it->second) << "/" << get<2>(it->second) << endl;
  }
}

/*
  A function that accesses the tuple of the specified user from the list of active users.
*/
tuple<string, string, string> get_user(const string& userid)
{
  cout << "Accessing the user " << userid << endl;
  return active_users.at(userid);
}

/*
  A function that removes the specified user from the list of active users.
  Used when signing out a user
*/
void remove_user(const string& userid)
{
  cout << "Removing the user " << userid << endl;
  active_users.erase(userid);

  for(auto it = active_users.begin(); it != active_users.end(); ++it)
  {
    cout << "\tUser " << it->first << ": " << get<1>(it->second) << "/" << get<2>(it->second) << endl;
  }
}

/*
  Prints active user list
*/
void active_users_list()
{
  for(auto it = active_users.begin(); it != active_users.end(); ++it)
  {
    cout << "\tUser " << it->first << ": " << get<1>(it->second) << "/" << get<2>(it->second) << endl;
  }
}


////////////////////////////////////////////////////////////////////////////



/*
  Top-level routine for processing all HTTP GET requests.
 */
void handle_get(http_request message) { 
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** UserServer GET " << path << endl;
  auto paths = uri::split_path(path);

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 3                        //
  //        Start of Josh's code - Get user's friend list        //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  if(paths[0] == read_friend_list)
  {
      cout << "Inside Josh's code for GET user's friend list for " << paths[1] << ".\n";

      string username = paths[1];

      // Check if the user has an active session.
      std::unordered_map<string, tuple<string, string, string>>::iterator active_user = active_users.find(username);

      if(active_user == active_users.end())
      {
        cout << "The user never had an active session.\n";
        message.reply(status_codes::Forbidden);
        return;
      }

      tuple<string, string, string> user_properties = get_user(username);
      cout << "\tUser token: " << get<0>(user_properties) << endl;
      cout << "\tUser partition: " << get<1>(user_properties) << endl;
      cout << "\tUser row: " << get<2>(user_properties) << endl;

      string user_token = get<0>(user_properties);
      string user_partition = get<1>(user_properties);
      string user_row = get<2>(user_properties);

      // Get the user's friend list.
      pair<status_code, value> signed_on_result
      {
        do_request (methods::GET,
                    basic_url + read_entity_auth + "/" + data_table_name + "/" + user_token + "/" + user_partition + "/" + user_row)
      };
      cout << "BasicServer access response " << signed_on_result.first << endl;

      if(signed_on_result.first == status_codes::BadRequest || signed_on_result.first == status_codes::NotFound)
      {
        cout << "Getting user's status to signed in, authorized, was unsuccessful.\n";
        message.reply(status_codes::NotFound);
        return;
      }

      unordered_map<string, string> data_properties = unpack_json_object(signed_on_result.second);

      for(auto it = data_properties.begin(); it != data_properties.end(); ++it)
      {
        cout << "\tData Property " << it->first << ": " << it->second << "\n"; 
      }

      // If the user has an active session, get the user's friend list from DataTable, authorized.
      string actual_friends = data_properties[friends];

      friends_list_t j = parse_friends_list(actual_friends);

      cout << "\nPRINTING FRIENDS --------------------------------------------\n\n";
      for(pair<string,string> s : j)
      {
        cout << s.first << ": " << s.second << endl;
      }

      // After all of these, signing in is finished and successful. Return status code "OK" and the update token
      if(signed_on_result.first == status_codes::OK)
      {
          cout << "Getting user's friend list was successful!\n";
          value friends_json { build_json_value (friends, actual_friends) };
          message.reply(status_codes::OK, friends_json);
          return;
      }

      cout << "At the end of ReadFriendList block. Nothing was done.\n";
  }

  // If the message gave a malformed request, return a BadRequest
  message.reply(status_codes::BadRequest);
  return;

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 3                        //
  //        Start of Josh's code - Get user's friend list        //
  //                                                             //
  /////////////////////////////////////////////////////////////////
}

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** UserServer POST " << path << endl;
  auto paths = uri::split_path(path);

  // Need at least the method and the username
  if(paths.size() < 2)
  {
      cout << "Paths has a size less than 2.\n";
      message.reply(status_codes::BadRequest);
      return;
  }
  
  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 3                        //
  //                Start of Josh's code - Sign On               //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  if(paths[0] == sign_on)
  {
      cout << "Inside Josh's code for POST Signing On for " << paths[1] << ".\n";

      // Access the JSON object of the message. It should have exactly one property: Password
      unordered_map<string, string> orig_properties = get_json_body(message);

      for(auto it = orig_properties.begin(); it != orig_properties.end(); ++it)
      {
        cout << "Original Property " << it->first << ": " << it->second << "\n"; 
      }

      if (orig_properties.size() != 1) {
        cout << "Your JSON body does not contain exactly 1 property.\n";
        message.reply(status_codes::NotFound);
        return;
      } else if (orig_properties[auth_table_password_prop].empty()) {
        cout << "Properties is empty.\n";
        message.reply(status_codes::NotFound);
        return;
      }

      // Aside: Check if the username only contains alphabetical characters.
      for (char c : paths[1]) {
          if (!(( 65 <= c && c <= 90) || (97 <= c && c <= 122))) {
              cout << "The username contains non-alphabetical characters.\n";
              message.reply(status_codes::NotFound);
              return;
          }
      }

      string username = paths[1];
      string password = orig_properties[auth_table_password_prop];

      // GetUpdateData from AuthTable; check if entry exists in AuthServer
      value password_json { build_json_value (auth_table_password_prop, password) };

      pair<status_code, value> auth_result
      {
        do_request (methods::GET, 
                    auth_url + get_update_data + "/" + username,
                    password_json)
      };
      cout << "AuthServer token response " << auth_result.first << endl;

      if(auth_result.first == status_codes::NotFound || auth_result.first == status_codes::BadRequest)
      {
        cout << "GetUpdateData from AuthTable was unsuccessful. AuthServer responded either not found or bad request.\n";
        message.reply(status_codes::NotFound);
        return;
      }

      unordered_map<string, string> data_properties = unpack_json_object(auth_result.second);

      for(auto it = data_properties.begin(); it != data_properties.end(); ++it)
      {
        cout << "Data Property " << it->first << ": " << it->second << "\n"; 
      }

      string token = data_properties["token"];
      string partition = data_properties[auth_table_partition_prop];
      string row = data_properties[auth_table_row_prop];

      // If GetUpdateToken was successful, check if entry exists in BasicServer
      pair<status_code, value> basic_result
      {
        do_request (methods::GET,
                    basic_url + read_entity + "/" + data_table_name + "/" + partition + "/" + row)
      };
      cout << "BasicServer entry response " << basic_result.first << endl;

      if(basic_result.first == status_codes::NotFound)
      {
        cout << "Getting entry from DataTable was unsuccessful. BasicServer responded not found.\n";
        message.reply(status_codes::NotFound);
        return;
      }

      // If the entry is found in both AuthServer and BasicServer, check if the user is already in the list
      // of active users. If he is, do nothing. If not, add the user in.
      std::unordered_map<string, tuple<string, string, string>>::iterator active_user = active_users.find(username);

      if(active_user == active_users.end())
      {
        cout << "The user never had an active session. He will be added to the list of active users.\n";
        add_user(username, token, partition, row);
      }

      // After all of these, signing in is finished and successful. Return status code "OK" and the update token
      if(auth_result.first == status_codes::OK && basic_result.first == status_codes::OK)
      {
          cout << "Signing On was successful!\n";
          message.reply(status_codes::OK);
          return;
      }

      cout << "At the end of Sign On block. Nothing was done.\n";
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 3                        //
  //                 End of Josh's code - Sign On                //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 3                        //
  //               Start of Josh's code - Sign Off               //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  if(paths[0] == sign_off)
  {
      cout << "Inside Josh's code for POST Signing Off for " << paths[1] << ".\n";

      string username = paths[1];

      // Find the user in the list of active users.
      std::unordered_map<string, tuple<string, string, string>>::iterator active_user = active_users.find(username);

      if(active_user == active_users.end())
      {
        cout << "The user never had an active session.\n";
        message.reply(status_codes::NotFound);
        return;
      }
      else
      {
        remove_user(username);
        cout << "Signing Off was successful!\n";
        message.reply(status_codes::OK);
        return;
      }
      
      cout << "At the end of Sign Off block. Nothing was done.\n";
  }

  cout << "\nPRINTING ACTIVE USER LIST -------------------------------\n\n";
  active_users_list();

  // If the message gave a malformed request, return a BadRequest
  message.reply(status_codes::BadRequest);
  return;

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 3                        //
  //                End of Josh's code - Sign Off                //
  //                                                             //
  /////////////////////////////////////////////////////////////////
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** UserServer PUT " << path << endl;
  auto paths = uri::split_path(path);
  
    ////////////////////////////////////////////////////////////////
    //                                                            //
    //                       ASSIGNMENT # 3                       //
    //                Start of Angel's code - Add Friend          //
    //                                                            //
    ////////////////////////////////////////////////////////////////

    if (paths[0] == add_friend) {
    
        string user_name {paths[1]};
        string friend_country {paths[2]};
        string friend_name {paths[3]};

        std::unordered_map<string, tuple<string, string, string>>::iterator active_user = active_users.find(user_name);

        if(active_user == active_users.end()) {
            cout << "The user never had an active session.\n";
            message.reply(status_codes::Forbidden);
            return;
        }

        tuple<string, string, string> user_properties = get_user(user_name);
        cout << "\tUser token: " << get<0>(user_properties) << endl;
        cout << "\tUser partition: " << get<1>(user_properties) << endl;
        cout << "\tUser row: " << get<2>(user_properties) << endl;

        string user_token = get<0>(user_properties);
        string user_partition = get<1>(user_properties);
        string user_row = get<2>(user_properties);


        pair<status_code, value> signed_on_result
        {
        do_request (methods::GET,
                    basic_url + read_entity_auth + "/" + data_table_name + "/" + user_token + "/" + user_partition + "/" + user_row)
        };
        cout << "BasicServer access response " << signed_on_result.first << endl;

        if(signed_on_result.first == status_codes::BadRequest || signed_on_result.first == status_codes::NotFound) {
            cout << "Getting user's status to signed in, authorized, was unsuccessful.\n";
            message.reply(status_codes::NotFound);
            return;
        }

        unordered_map<string, string> data_properties = unpack_json_object(signed_on_result.second);

        for(auto it = data_properties.begin(); it != data_properties.end(); ++it) {
            cout << "\tData Property " << it->first << ": " << it->second << "\n"; 
        }

        // If the user has an active session, get the user's friend list from DataTable, authorized.
        string friend_list = data_properties[friends];

        //If the user has an active session, add a friend to their friends list
        auto compare = make_pair(friend_country, friend_name);

        vector<pair<string, string>> friend_vector = parse_friends_list(friend_list);

        bool friend_found = false;
        for(auto it = friend_vector.begin(); it != friend_vector.end(); ++it) {
            if (it->first == friend_country && it->second == friend_name) {

                cout << "Friend " + it-> second + " is already on friends list\n";
                friend_found = true;
                message.reply(status_codes::OK);
                return;
            }
        }

        if(!friend_found) {
            cout<< "Friend was not on list -- adding friend to vector\n";
            friend_vector.push_back(make_pair(friend_country, friend_name));
            cout << "Current friends in vector :" << endl;
            for(auto it = friend_vector.begin(); it != friend_vector.end(); ++it) {
                cout << "\t" + it->first + ";" + it->second << endl;
            }
        }


        friend_list = friends_list_to_string(friend_vector);
        value updates_friend {build_json_value(friends, friend_list)};
        
        // Update the user's friend list
        cout << "Adding friend: " << friend_country << ";" << friend_name << endl;

        pair<status_code, value> friend_result
        {
          do_request(methods::PUT,
                basic_url + update_entity + "/" + data_table_name + "/" + user_partition + "/" + user_row,updates_friend)
        };
        cout << "BasicServer access response: " << friend_result.first << endl;

        if(friend_result.first == status_codes::OK) {
            cout << "Adding friend " + friend_name + " was successful\n";
            message.reply(status_codes::OK);
            return;
        }
    }

    ////////////////////////////////////////////////////////////////
    //                                                            //
    //                       ASSIGNMENT # 3                       //
    //                End of Angel's code - Add Friend            //
    //                                                            //
    ////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////
    //                                                            //
    //                       ASSIGNMENT # 3                       //
    //                Start of Angel's code - UnFriend            //
    //                                                            //
    ////////////////////////////////////////////////////////////////

    if (paths[0] == unfriend) {
    
        string user_name {paths[1]};
        string friend_country {paths[2]};
        string friend_name {paths[3]};

        std::unordered_map<string, tuple<string, string, string>>::iterator active_user = active_users.find(user_name);

        if(active_user == active_users.end()) {
            cout << "The user never had an active session.\n";
            message.reply(status_codes::Forbidden);
            return;
        }

        tuple<string, string, string> user_properties = get_user(user_name);
        cout << "\tUser token: " << get<0>(user_properties) << endl;
        cout << "\tUser partition: " << get<1>(user_properties) << endl;
        cout << "\tUser row: " << get<2>(user_properties) << endl;

        string user_token = get<0>(user_properties);
        string user_partition = get<1>(user_properties);
        string user_row = get<2>(user_properties);

        pair<status_code, value> signed_on_result
        {
        do_request (methods::GET,
                    basic_url + read_entity_auth + "/" + data_table_name + "/" + user_token + "/" + user_partition + "/" + user_row)
        };
        cout << "BasicServer access response " << signed_on_result.first << endl;

        if(signed_on_result.first == status_codes::BadRequest || signed_on_result.first == status_codes::NotFound) {
            cout << "Getting user's status to signed in, authorized, was unsuccessful.\n";
            message.reply(status_codes::NotFound);
            return;
        }

        unordered_map<string, string> data_properties = unpack_json_object(signed_on_result.second);

        for(auto it = data_properties.begin(); it != data_properties.end(); ++it) {
            cout << "\tData Property " << it->first << ": " << it->second << "\n"; 
        }

        // If the user has an active session, get the user's friend list from DataTable, authorized.
        string friend_list = data_properties[friends];

        // Parse through the friend list and erase friend properties if found
        vector<pair<string, string>> friend_vector {parse_friends_list(friend_list)};

        //Output all the friends from the vector
        cout << "Initial vector of friends" << endl;
        for(auto it = friend_vector.begin(); it != friend_vector.end(); ++it) {
          cout << "\tFriend: " << it->second << " from " << it->first << endl;
        }

        bool friend_is_found = false;
        for(auto it = friend_vector.begin(); it != friend_vector.end(); ++it) {
            if (it->first == friend_country && it->second == friend_name) {
                cout << "Friend found\n";
                friend_vector.erase(it);
                friend_is_found = true;
                break;
            }
        }
        if(!friend_is_found)
        {
          cout << "Friend was not on friend list to begin with\n";
          message.reply(status_codes::OK);
          return;
        }

        //Output all the friends from the vector
        cout << "Final vector of friends" << endl;
        for(auto it = friend_vector.begin(); it != friend_vector.end(); ++it) {
          cout << "\tFriend: " << it->second << " from " << it->first << endl;
        }

        friend_list = friends_list_to_string(friend_vector);

        cout << "Final string of friends: " << friend_list << endl;


        value updates_friend {build_json_value(friends, friend_list)};

        // Update the user's friend list

        cout << "Removing friend " << friend_country << ";" << friend_name << endl;
        pair<status_code, value> friend_result
        {
            do_request(methods::PUT,
                        basic_url + update_entity + "/" + data_table_name + "/" + user_partition + "/" + user_row,updates_friend)
        };
        cout << "BasicServer access response: " << friend_result.first << endl;

        if(friend_result.first == status_codes::OK) {
            cout << "Removing friend " + friend_name + " was successful\n";
            message.reply(status_codes::OK);
            return;
        }
        // find friend that is to be removed in list and remove from vector
        // call friends_list_to_string and convert back into string
        // return the new friend list string
    }

    ////////////////////////////////////////////////////////////////
    //                                                            //
    //                       ASSIGNMENT # 3                       //
    //                End of Angel's code - UnFriend              //
    //                                                            //
    ////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////
    //                                                            //
    //                       ASSIGNMENT # 3                       //
    //             Start of Angel's code - Update Status          //
    //                                                            //
    ////////////////////////////////////////////////////////////////

    if (paths[0] == update_status) {

        string user_name {paths[1]};
        string status_up {paths[2]};

        std::unordered_map<string, tuple<string, string, string>>::iterator active_user = active_users.find(user_name);

        if(active_user == active_users.end()) {
            cout << "The user never had an active session.\n";
            message.reply(status_codes::Forbidden);
            return;
        }

        tuple<string, string, string> user_properties = get_user(user_name);
        cout << "\tUser token: " << get<0>(user_properties) << endl;
        cout << "\tUser partition: " << get<1>(user_properties) << endl;
        cout << "\tUser row: " << get<2>(user_properties) << endl;

        string user_token = get<0>(user_properties);
        string user_partition = get<1>(user_properties);
        string user_row = get<2>(user_properties);

        pair<status_code, value> signed_on_result
        {
        do_request (methods::GET,
                    basic_url + read_entity_auth + "/" + data_table_name + "/" + user_token + "/" + user_partition + "/" + user_row)
        };
        cout << "BasicServer access response " << signed_on_result.first << endl;

        if(signed_on_result.first == status_codes::BadRequest || signed_on_result.first == status_codes::NotFound) {
            cout << "Getting user's status to signed in, authorized, was unsuccessful.\n";
            message.reply(status_codes::NotFound);
            return;
        }

        unordered_map<string, string> data_properties = unpack_json_object(signed_on_result.second);

        for(auto it = data_properties.begin(); it != data_properties.end(); ++it) {
            cout << "\tData Property " << it->first << ": " << it->second << "\n"; 
        }

        // Update the status of the user.
        value update_stat {build_json_value(status, status_up)};
        value friends_json {build_json_value(friends, data_properties[friends])};

        string update_string {data_properties[updates]};
        update_string += status_up + "\n";

        value updateString {build_json_value(updates, update_string)};

        pair<status_code, value> update_res
        {
            do_request(methods::PUT,
                        basic_url + update_entity + "/" + data_table_name + "/" + user_partition + "/" + user_row,updateString)
        };
        cout << "BasicServer access response: " << update_res.first << endl;

        pair<status_code, value> update_stat_res
        {
            do_request(methods::PUT,
                        basic_url + update_entity + "/" + data_table_name + "/" + user_partition + "/" + user_row,update_stat)
        };
        cout << "BasicServer access response: " << update_stat_res.first << endl;

        pair<status_code, value> push_up_stat_res {};

        // Call PushServer to push the user's status to all his/her friends.
        try 
        {
            push_up_stat_res = 
            do_request(methods::POST,
                        push_url + push_status + "/" + user_partition + "/" + user_row + "/" + status_up, friends_json)
            ;
            cout << "PushServer access response: " << push_up_stat_res.first << endl;
        } 
        catch (const web::uri_exception& e) 
        {
            message.reply(status_codes::ServiceUnavailable);
            return;
        }

        if(update_stat_res.first == status_codes::OK && push_up_stat_res.first == status_codes::OK) {
            cout << "Update Status " + status_up + " was successful\n";
            message.reply(status_codes::OK);
            return;
        }

    }

    ////////////////////////////////////////////////////////////////
    //                                                            //
    //                       ASSIGNMENT # 3                       //
    //               End of Angel's code - Update Status          //
    //                                                            //
    ////////////////////////////////////////////////////////////////

  // If the message gave a malformed request, return a BadRequest
  message.reply(status_codes::BadRequest);
  return;
}

/*
  Top-level routine for processing all HTTP DELETE requests.
 */
void handle_delete(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** UserServer DELETE " << path << endl;
}

/*
  Main user server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.

  USER Server only supports the POST, PUT, and GET methods.
  If the you need the delete method called, you can uncomment it.
  
  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {
  cout << "UserServer: Parsing connection string" << endl;
  //table_cache.init (storage_connection_string);

  cout << "UserServer: Opening listener" << endl;
  http_listener listener {user_url};
  listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  listener.support(methods::PUT, &handle_put);
  //listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop UserServer." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "UserServer closed" << endl;
}