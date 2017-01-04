/*
  CMPT 276 with Dr. Ted Kirkpatrick (Spring 2016)

  Angel and the Saints
   - Josh Arik Miguel Fernandez
   - Angelina Singh
   - Woojin (Andrew) Song
   - Lawrence Yu

 Authorization Server code for CMPT 276, Spring 2016.
 */

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <cpprest/http_listener.h>
#include <cpprest/json.h>

#include <was/common.h>
#include <was/table.h>

#include "TableCache.h"
#include "make_unique.h"
#include "azure_keys.h"

using azure::storage::storage_exception;
using azure::storage::cloud_table;
using azure::storage::cloud_table_client;
using azure::storage::edm_type;
using azure::storage::entity_property;
using azure::storage::table_entity;
using azure::storage::table_operation;
using azure::storage::table_request_options;
using azure::storage::table_result;
using azure::storage::table_shared_access_policy;

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

/////////////////////////////////////////////////////
//                                                 //
//                   Servers Used                  //
//                                                 //
/////////////////////////////////////////////////////

constexpr const char* def_url = "http://localhost:34570";

/////////////////////////////////////////////////////
//                                                 //
//                   Methods Used                  //
//                                                 //
/////////////////////////////////////////////////////

const string auth_table_name {"AuthTable"};
const string auth_table_userid_partition {"Userid"};
const string auth_table_password_prop {"Password"};
const string auth_table_partition_prop {"DataPartition"};
const string auth_table_row_prop {"DataRow"};
const string data_table_name {"DataTable"};

const string get_read_token_op {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};
const string get_update_data {"GetUpdateData"};

const string get_update_data_op{ "GetUpdateData" };

/*
  Cache of opened tables
 */
TableCache table_cache {};

/*
  Convert properties represented in Azure Storage type
  to prop_str_vals_t type.
 */
prop_str_vals_t get_string_properties (const table_entity::properties_type& properties) {
  prop_str_vals_t values {};
  for (const auto v : properties) {
    if (v.second.property_type() == edm_type::string) {
      values.push_back(make_pair(v.first,v.second.string_value()));
    }
    else {
      // Force the value as string in any case
      values.push_back(make_pair(v.first, v.second.str()));
    }
  }
  return values;
}

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
  Return a token for 24 hours of access to the specified table,
  for the single entity defind by the partition and row.

  permissions: A bitwise OR ('|')  of table_shared_access_poligy::permission
    constants.

    For read-only:
      table_shared_access_policy::permissions::read
    For read and update: 
      table_shared_access_policy::permissions::read |
      table_shared_access_policy::permissions::update
 */
pair<status_code,string> do_get_token (const cloud_table& data_table,
                   const string& partition,
                   const string& row,
                   uint8_t permissions) {

  cout << "Inside DoGetToken in AuthServer.\n";

  utility::datetime exptime {utility::datetime::utc_now() + utility::datetime::from_days(1)};
  try {
    string limited_access_token {
      data_table.get_shared_access_signature(table_shared_access_policy {
                                               exptime,
                                               permissions},
                                             string(), // Unnamed policy
                                             // Start of range (inclusive)
                                             partition,
                                             row,
                                             // End of range (inclusive)
                                             partition,
                                             row)
        // Following token allows read access to entire table
        //table.get_shared_access_signature(table_shared_access_policy {exptime, permissions})
      };
    cout << "Token " << limited_access_token << endl;
    return make_pair(status_codes::OK, limited_access_token);
  }
  catch (const storage_exception& e) {
    cout << "Azure Table Storage error: " << e.what() << endl;
    cout << e.result().extended_error().message() << endl;
    return make_pair(status_codes::InternalError, string{});
  }
}

/*
  Top-level routine for processing all HTTP GET requests.
 */
void handle_get(http_request message) { 
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** AuthServer GET " << path << endl;
  auto paths = uri::split_path(path);

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                        ASSIGNMENT # 2                       //
  //                   Start of Lawrence's code                  //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  cloud_table auth_table{ table_cache.lookup_table(auth_table_name) };

  if (!auth_table.exists()) {
      cout << "AuthTable does not exist.\n";
      message.reply(status_codes::NotFound);
      return;
  }
  
  cloud_table data_table{ table_cache.lookup_table(data_table_name) };

  if (!data_table.exists()) {
      cout << "DataTable does not exist.\n";
      message.reply(status_codes::NotFound);
      return;
  }
 
  unordered_map<string, string> properties = get_json_body(message);
 
  if (paths.size() != 2) {
    cout << "Paths size does not equal 2.\n";
    message.reply(status_codes::BadRequest);
    return;
  } else if (properties.size() != 1) {
    cout << "Your JSON body does not contain exactly 1 property.\n";
    message.reply(status_codes::BadRequest);
    return;
  } else if (properties[auth_table_password_prop].empty()) {
    cout << "Properties is empty.\n";
    message.reply(status_codes::BadRequest);
    return;
  }
 
  for (char c : properties[auth_table_password_prop]) {
      if (c > 127 || c < 0) {
          cout << "The password contains non-ASCII7 characters.\n";
          message.reply(status_codes::BadRequest);
          return;
      }
  }

  string password = properties[auth_table_password_prop];
 
  // GET specific entry: Partition == paths[1], Row == paths[2]
  table_operation retrieve_operation{ table_operation::retrieve_entity(auth_table_userid_partition ,paths[1])};
  table_result retrieve_result{ auth_table.execute(retrieve_operation) };
 
  cout << "HTTP code: " << retrieve_result.http_status_code() << endl;

  if (retrieve_result.http_status_code() == status_codes::NotFound) {
      cout << "Cannot get a specific entry, or entry is not found.\n";
      message.reply(status_codes::NotFound);
      return;
  }
  
  // GET the JSON body of the entry: Username and Password
  table_entity entity{ retrieve_result.entity() };
  table_entity::properties_type properties1{ entity.properties() };
  prop_str_vals_t values(get_string_properties(properties1));

  // Output the contents of values.
  cout << "--The contents of values.--" << endl;
  for (pair<string, string> value : values) {
    cout << "\t" << value.first << " : " << value.second << endl;
  }

  if (values.size() == 3) {
      for (pair<string, string> value : values) {
          if (value.first == auth_table_password_prop) {
              if (value.second != password) {
                  cout << "Password is not the same.\n";
                  message.reply(status_codes::NotFound);
                  return;
              }
          }
      }
  } else {
      cout << "The size of values does not equal 3.\n";
      message.reply(status_codes::NotFound);
      return;
  }
 
  string partition = " ";
  string row = " ";
 
  for (pair<string, string> value : values) {
      if (value.first == auth_table_partition_prop) {
          partition = value.second;
      }
  }
 
  for (pair<string, string> value : values) {
      if (value.first == auth_table_row_prop) {
          row = value.second;
      }
  }
 
  if (paths[0] == get_update_token_op) {
      cout << "GetUpdateToken was called and succeeded.\n";
      pair<status_code, string> result = do_get_token(data_table, partition, row, table_shared_access_policy::permissions::read |
          table_shared_access_policy::permissions::update);
      vector<pair<string, value>> token{ make_pair("token", value::string(result.second)) };
      message.reply(result.first, value::object(token));
      return;
  } else if (paths[0] == get_read_token_op) {
      cout << "GetReadToken was called and succeeded.\n";
      pair<status_code, string> result = do_get_token(data_table, partition, row, table_shared_access_policy::permissions::read);
      vector<pair<string, value>> token{ make_pair("token", value::string(result.second)) };
      message.reply(result.first, value::object(token));
      return;
  } else if (paths[0] == get_update_data) {
      cout << "GetUpdateData was called and succeeded.\n";
      pair<status_code, string> result = do_get_token(data_table, partition, row, table_shared_access_policy::permissions::read |
          table_shared_access_policy::permissions::update);
      vector<pair<string, value>> token{ make_pair("token", value::string(result.second)),
                                         make_pair(auth_table_partition_prop, value::string(partition)),
                                         make_pair(auth_table_row_prop, value::string(row))};
      message.reply(result.first, value::object(token));
      return;
  }
  cout << "At the end of AuthServer's handle_get. Nothing was done.\n";
  message.reply(status_codes::BadRequest);
}

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                        ASSIGNMENT # 2                       //
  //                    End of Lawrence's code                   //
  //                                                             //
  /////////////////////////////////////////////////////////////////

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PUT " << path << endl;
}

/*
  Top-level routine for processing all HTTP DELETE requests.
 */
void handle_delete(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** DELETE " << path << endl;
}

/*
  Main authentication server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.

  Note that, unlike BasicServer, AuthServer only
  installs the listeners for GET. Any other HTTP
  method will produce a Method Not Allowed (405)
  response.

  If you want to support other methods, uncomment
  the call below that hooks in a the appropriate 
  listener.
  
  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {
  cout << "AuthServer: Parsing connection string" << endl;
  table_cache.init (storage_connection_string);

  cout << "AuthServer: Opening listener" << endl;
  http_listener listener {def_url};
  listener.support(methods::GET, &handle_get);
  //listener.support(methods::POST, &handle_post);
  //listener.support(methods::PUT, &handle_put);
  //listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop AuthServer." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "AuthServer closed" << endl;
}
