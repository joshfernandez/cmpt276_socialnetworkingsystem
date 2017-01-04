/*
  CMPT 276 with Dr. Ted Kirkpatrick (Spring 2016)

  Angel and the Saints
   - Josh Arik Miguel Fernandez
   - Angelina Singh
   - Woojin (Andrew) Song
   - Lawrence Yu

 Basic Server code for CMPT 276, Spring 2016.
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

#include "azure_keys.h"
 #include "ServerUtils.h"

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
using azure::storage::table_result;

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
using web::http::status_codes;
using web::http::uri;

using web::json::value;

using web::http::experimental::listener::http_listener;

using prop_vals_t = vector<pair<string,value>>;

/////////////////////////////////////////////////////
//                                                 //
//                   Servers Used                  //
//                                                 //
/////////////////////////////////////////////////////

constexpr const char* def_url = "http://localhost:34568";

/////////////////////////////////////////////////////
//                                                 //
//                   Methods Used                  //
//                                                 //
/////////////////////////////////////////////////////

const string create_table {"CreateTableAdmin"};
const string delete_table {"DeleteTableAdmin"};
const string update_entity {"UpdateEntityAdmin"};
const string delete_entity {"DeleteEntityAdmin"};
const string update_property {"UpdatePropertyAdmin"};
const string add_property {"AddPropertyAdmin"};
const string read_entity {"ReadEntityAdmin"};
const string update_entity_auth{"UpdateEntityAuth"};
const string read_entity_auth{"ReadEntityAuth"};


/*
  Cache of opened tables
 */
TableCache table_cache {};

/*
  Convert properties represented in Azure Storage type
  to prop_vals_t type.
 */
prop_vals_t get_properties (const table_entity::properties_type& properties, prop_vals_t values = prop_vals_t {}) {
  for (const auto v : properties) {
    if (v.second.property_type() == edm_type::string) {
      values.push_back(make_pair(v.first, value::string(v.second.string_value())));
    }
    else if (v.second.property_type() == edm_type::datetime) {
      values.push_back(make_pair(v.first, value::string(v.second.str())));
    }
    else if(v.second.property_type() == edm_type::int32) {
      values.push_back(make_pair(v.first, value::number(v.second.int32_value())));      
    }
    else if(v.second.property_type() == edm_type::int64) {
      values.push_back(make_pair(v.first, value::number(v.second.int64_value())));      
    }
    else if(v.second.property_type() == edm_type::double_floating_point) {
      values.push_back(make_pair(v.first, value::number(v.second.double_value())));      
    }
    else if(v.second.property_type() == edm_type::boolean) {
      values.push_back(make_pair(v.first, value::boolean(v.second.boolean_value())));      
    }
    else {
      values.push_back(make_pair(v.first, value::string(v.second.str())));
    }
  }
  return values;
}

/*
  Return true if an HTTP request has a JSON body

  This routine can be called multiple times on the same message.
 */
bool has_json_body (http_request message) {
  return message.headers()["Content-type"] == "application/json";
}

/*
  Given an HTTP message with a JSON body, return the JSON
  body as an unordered map of strings to strings.

  If the message has no JSON body, return an empty map.

  THIS ROUTINE CAN ONLY BE CALLED ONCE FOR A GIVEN MESSAGE
  (see http://microsoft.github.io/cpprestsdk/classweb_1_1http_1_1http__request.html#ae6c3d7532fe943de75dcc0445456cbc7
  for source of this limit).

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

  GET is the only request that has no command. All
  operands specify the value(s) to be retrieved.
 */
void handle_get(http_request message) { 

  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** GET " << path << endl;
  auto paths = uri::split_path(path);
  // Need at least a table name
  if (paths.size() < 2) {
    cout << "Paths has a size less than 2.\n";
    message.reply(status_codes::BadRequest);
    return;
  }

  cloud_table table {table_cache.lookup_table(paths[1])};
  if ( ! table.exists()) {
    cout << "The table does not exist.\n";
    message.reply(status_codes::NotFound);
    return;
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 2                        //
  //                   Start of Andrew's code                    //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // READ ENTITY WITH AUTHORIZATION ~DANK MEME CODE~
  // COMMAND, TB NAME, TOKEN, PART, ROW

  if (paths[0] == read_entity_auth){
    cout << "Inside Andrew's code for Authorized GET.\n";

    // use ServerUtils.cpp function: read_with_token to get status code and entity
    auto read_entity = read_with_token(message, tables_endpoint);

    if (paths.size() < 5) {
      message.reply(status_codes::BadRequest);
      return;
    }

    if (read_entity.first != status_codes::OK){
      // return read_with_token status code
      cout << "Read with token was unsuccessful.\n";
      message.reply(read_entity.first);
      return;
    }

    // get entity
    table_entity entity = read_entity.second;
    // get properties
    table_entity::properties_type properties {entity.properties()};
  
    // If the entity has any properties, return them as JSON
    prop_vals_t values (get_properties(properties));
    if (values.size() > 0){
      message.reply(status_codes::OK, value::object(values));
      return;
    } else {
      cout << "No properties" << endl;
      message.reply(status_codes::OK);
      return;
    }
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 2                        //
  //                    End of Andrew's code                     //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 1                        //
  //               Start of Andrew's code (Part 1)               //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // GET all entities from a specific partition, row == *

  if (paths.size() == 4 && paths[3] == "*") {

    cout << "Inside Andrew's code for GET.\n";

    table_query query {};
    table_query_iterator end;
    table_query_iterator it = table.execute_query(query);
    vector<value> key_vec;
    while (it != end) {
      if (it->partition_key() == paths[2]){
      cout << "Key: " << it->partition_key() << " / " << it->row_key() << endl;
      prop_vals_t keys {
        make_pair("Partition", value::string(it->partition_key())),
        make_pair("Row", value::string(it->row_key()))};
        keys = get_properties(it->properties(), keys);
        key_vec.push_back(value::object(keys));
      }
      ++it;
    }
    message.reply(status_codes::OK, value::array(key_vec));
    return;
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 1                        //
  //                End of Andrew's code (Part 1)                //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 1                        //
  //              Start of Lawrence's code (Part 1)              //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // GET all entities containing all specified properties
  unordered_map<string,string> properties1 = get_json_body(message);

  if (properties1.size() > 0 && paths.size() == 2) { // You only want the TableName

    cout << "Inside Lawrence's code for GET.\n";

    vector<string> v;

    //Make a vector of all different types of properties.
    for (auto it = properties1.begin(); it != properties1.end(); ++it ) {
      if(it->second == "*") {
        v.push_back(it->first);
      }
    }

    // Set up your iterators -- beginning and end
    table_query query {};
    table_query_iterator end;
    table_query_iterator it = table.execute_query(query);
    vector<value> key_vec;
    bool contains_property = false;

    //Go through all the entries inside the table.
    while (it != end) {
      prop_vals_t keys {
        make_pair("Partition",value::string(it->partition_key())),
        make_pair("Row", value::string(it->row_key()))
      };
      keys = get_properties(it->properties(), keys); // Get the properties of each entry.

      for(int i = 0; i < v.size(); ++i) {
        contains_property = false;
        for(int j = 0; j < keys.size(); ++j) {
          if(keys[j].first == v[i]) { //If a property is in the list of specified properties
            contains_property = true;
            break;
          }
        }
        if(contains_property == false)
        {
          break;
        }
      }

      if(contains_property == true) {
        cout << "Key: " << it->partition_key() << " / " << it->row_key() << endl;

        // Josh's code added here
        for(int i = 0; i < keys.size(); i++)
        {
          cout << "\tProperty " << i << " | " << keys[i].first << ": " << keys[i].second << endl;
        }
        // Josh's code ends here

        cout << endl;
        key_vec.push_back(value::object(keys));  
      }
      ++it;
    }
    message.reply(status_codes::OK, value::array(key_vec));
    return;
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 1                        //
  //                End of Lawrence's code (Part 1)              //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // GET all entries in table
  if (paths.size() == 2 && paths[0] == read_entity) {
    table_query query {};
    table_query_iterator end;
    table_query_iterator it = table.execute_query(query);
    vector<value> key_vec;
    while (it != end) {
      cout << "Key: " << it->partition_key() << " / " << it->row_key() << endl;
      prop_vals_t keys {
  make_pair("Partition",value::string(it->partition_key())),
  make_pair("Row", value::string(it->row_key()))};
      keys = get_properties(it->properties(), keys);
      key_vec.push_back(value::object(keys));
      ++it;
    }
    message.reply(status_codes::OK, value::array(key_vec));
    return;
  }

  // GET specific entry: Partition == paths[1], Row == paths[2]
  if (paths.size() != 4 && paths[0] != read_entity) {
    cout << "The specific entry does not equal 4.\n";
    message.reply (status_codes::BadRequest);
    return;
  }

  table_operation retrieve_operation {table_operation::retrieve_entity(paths[2], paths[3])};
  table_result retrieve_result {table.execute(retrieve_operation)};
  cout << "HTTP code: " << retrieve_result.http_status_code() << endl;
  if (retrieve_result.http_status_code() == status_codes::NotFound) {
    message.reply(status_codes::NotFound);
    return;
  }

  table_entity entity {retrieve_result.entity()};
  table_entity::properties_type properties {entity.properties()};
  
  // If the entity has any properties, return them as JSON
  prop_vals_t values (get_properties(properties));
  if (values.size() > 0)
    message.reply(status_codes::OK, value::object(values));
  else
    message.reply(status_codes::OK);
}

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
  auto paths = uri::split_path(path);
  // Need at least an operation and a table name
  if (paths.size() < 2) {
    message.reply(status_codes::BadRequest);
    return;
  }

  string table_name {paths[1]};
  cloud_table table {table_cache.lookup_table(table_name)};

  // Create table (idempotent if table exists)
  if (paths[0] == create_table) {
    cout << "Create " << table_name << endl;
    bool created {table.create_if_not_exists()};
    cout << "Administrative table URI " << table.uri().primary_uri().to_string() << endl;
    if (created)
      message.reply(status_codes::Created);
    else
      message.reply(status_codes::Accepted);
  }
  else {
    message.reply(status_codes::BadRequest);
  }
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PUT " << path << endl;
  auto paths = uri::split_path(path);

  unordered_map<string,string> json_body {get_json_body (message)};  

  cloud_table table{ table_cache.lookup_table(paths[1]) };
  if (!table.exists()) {
    message.reply(status_codes::NotFound);
    return;
  }

  if(paths.size() < 2)
  {
    message.reply(status_codes::BadRequest);
    return;
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                        ASSIGNMENT # 2                       //
  //                   Start of Lawrence's code                  //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  if (paths[0] == update_entity_auth)
  {

    if (paths.size() < 5) {
      message.reply(status_codes::BadRequest);
      return;
    }

      cout << "Inside Lawrence's code for Authorized PUT.\n";

      if (paths.size() < 5) {
        message.reply(status_codes::BadRequest);
        return;
      }

      try {
          web::http::status_code result = update_with_token(message, tables_endpoint, json_body);

          /////////////////// Start of Josh's code ////////////////////

          cout << "---Authorized PUT: All the entries in DataTable---\n";

          // Set up your iterators
          table_query query{};
          table_query_iterator end;
          table_query_iterator it = table.execute_query(query);
          vector<pair<utility::string_t, utility::string_t>> partition_and_row_vector;

          while (it != end) {
            // Get a specific entry
            table_operation retrieve_operation {table_operation::retrieve_entity(it->partition_key(), it->row_key())};
            table_result retrieve_result {table.execute(retrieve_operation)};

            // Get all the properties in that entry
            table_entity entity {retrieve_result.entity()};
            table_entity::properties_type& properties = entity.properties();

            // Output all the properties of that entry
            cout << "Key: " << it->partition_key() << " / " << it->row_key() << endl;
            for ( auto it = properties.begin(); it != properties.end(); ++it ) {
              cout << "\tProperty Name: " << it->first  << ", Property Value: " << value::string(it->second.string_value()) << endl;
            }

            ++it;
          }

          /////////////////// End of Josh's code ////////////////////

          cout << "Authorized PUT succeeds " << endl;
          message.reply(result);
          return;
      }
      catch (const storage_exception& e) {
          cout << "Azure Table Storage error: " << e.what() << endl;
          cout << e.result().extended_error().message() << endl;
          if (e.result().http_status_code() == status_codes::Forbidden)
              message.reply(status_codes::Forbidden);
          else
              message.reply(status_codes::InternalError);
      }
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                        ASSIGNMENT # 2                       //
  //                    End of Lawrence's code                   //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 1                        //
  //               Start of Andrew's code (Part 2)               //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // Add the specified property to all entities
  if (paths.size() == 2 && paths[0] == add_property){

    cout << "Inside Andrew's code for PUT.\n";
    
    if (json_body.size() > 0) {

      utility::string_t property_name;
      entity_property property_value;

      // get prop name and values
      for (auto& x : json_body) {
        property_name = x.first;
        property_value = x.second;
      }

      // go through all the entities

      table_query query{};
      table_query_iterator end;
      table_query_iterator it = table.execute_query(query);
      vector<pair<utility::string_t, utility::string_t>> partition_and_row_vector;

      while (it != end) {

        table_operation retrieve_operation {table_operation::retrieve_entity(it->partition_key(), it->row_key())};
        table_result retrieve_result {table.execute(retrieve_operation)};

        table_entity entity {retrieve_result.entity()};
        table_entity::properties_type& properties = entity.properties();

        // adds property to all entities or if already existing, will replace
        // the property_value 
        properties[property_name] = property_value;

        table_operation operation{ table_operation::insert_or_merge_entity(entity) };
        table_result op_result{ table.execute(operation) };

        ++it;
      }

      // table found and added to all entities
      message.reply(status_codes::OK);
      return;
    } else {
      message.reply(status_codes::BadRequest);
      return;
    }
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 1                        //
  //                End of Andrew's code (Part 2)                //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 1                        //
  //               Start of Lawrence's code (Part 2)             //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // Update the specified property in all entities
  if (paths.size() == 2 && paths[0] == update_property)
  {

    cout << "Inside Lawrence's code for PUT.\n";
    //unordered_map<string, string> json_body = get_json_body(message);

    if (paths[0] == update_property && json_body.size() > 0) {

      utility::string_t property_name;
      entity_property property_value;

      for ( auto it = json_body.begin(); it != json_body.end(); ++it ) {
        if (it->first != "") {
          property_name = it->first;
          property_value = it->second;
          break;
        }
      }
      
      // Set up your iterators
      table_query query{};
      table_query_iterator end;
      table_query_iterator it = table.execute_query(query);
      vector<pair<utility::string_t, utility::string_t>> partition_and_row_vector;

      while (it != end) {
        // Get a specific entry
        table_operation retrieve_operation {table_operation::retrieve_entity(it->partition_key(), it->row_key())};
        table_result retrieve_result {table.execute(retrieve_operation)};

        // Get all the properties in that entry
        table_entity entity {retrieve_result.entity()};
        table_entity::properties_type& properties = entity.properties();

        // Check if the correct property is there. If it does, update its property value.
        for ( auto it = properties.begin(); it != properties.end(); ++it ) {
          if (it->first == property_name) {
            properties[property_name] = property_value;
            cout << "Update " << entity.partition_key() << " / " << entity.row_key() << endl;
            cout << "\tProperty Name: " << it->first << endl;
            cout << "\tProperty Value: " << value::string(it->second.string_value()) << endl;
          }
        }

        table_operation operation{ table_operation::insert_or_merge_entity(entity) };
        table_result op_result{ table.execute(operation) };

        ++it;
      }

      message.reply(status_codes::OK);
      return;
    }
    else {
      message.reply(status_codes::BadRequest);
      return;
    }
  }

  /////////////////////////////////////////////////////////////////
  //                                                             //
  //                       ASSIGNMENT # 1                        //
  //                End of Lawrence's code (Part 2)              //
  //                                                             //
  /////////////////////////////////////////////////////////////////

  // Need at least an operation, table name, partition, and row
  if (paths.size() < 4) {
    cout << "Paths does not have an operation, table name, partition, and row.\n";
    message.reply(status_codes::BadRequest);
    return;
  }

  table_entity entity {paths[2], paths[3]};

  table = table_cache.lookup_table(paths[1]);
  if (!table.exists()) {
    cout << "Table does not exist... again.\n";
    message.reply(status_codes::NotFound);
  }

  // Update entity
  try {
    if (paths[0] == update_entity) {
      cout << "Update " << entity.partition_key() << " / " << entity.row_key() << endl;
      table_entity::properties_type& properties = entity.properties();
      for (const auto v : json_body) {
	       properties[v.first] = entity_property {v.second};
      }

      table_operation operation {table_operation::insert_or_merge_entity(entity)};
      table_result op_result {table.execute(operation)};

      message.reply(status_codes::OK);
    }
    else {
      cout << "Cannot update entity.\n";
      message.reply(status_codes::BadRequest);
    }
  }
  catch (const storage_exception& e)
  {
    cout << "Azure Table Storage error: " << e.what() << endl;
    message.reply(status_codes::InternalError);
  }
}

/*
  Top-level routine for processing all HTTP DELETE requests.
 */
void handle_delete(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** DELETE " << path << endl;
  auto paths = uri::split_path(path);
  // Need at least an operation and table name
  if (paths.size() < 2) {
  message.reply(status_codes::BadRequest);
  return;
  }

  string table_name {paths[1]};
  cloud_table table {table_cache.lookup_table(table_name)};

  // Delete table
  if (paths[0] == delete_table) {
    cout << "Delete " << table_name << endl;
    if ( ! table.exists()) {
      message.reply(status_codes::NotFound);
    }
    table.delete_table();
    table_cache.delete_entry(table_name);
    message.reply(status_codes::OK);
  }
  // Delete entity
  else if (paths[0] == delete_entity) {
    // For delete entity, also need partition and row
    if (paths.size() < 4) {
  message.reply(status_codes::BadRequest);
  return;
    }
    table_entity entity {paths[2], paths[3]};
    cout << "Delete " << entity.partition_key() << " / " << entity.row_key()<< endl;

    table_operation operation {table_operation::delete_entity(entity)};
    table_result op_result {table.execute(operation)};

    int code {op_result.http_status_code()};
    if (code == status_codes::OK || 
  code == status_codes::NoContent)
      message.reply(status_codes::OK);
    else
      message.reply(code);
  }
  else {
    message.reply(status_codes::BadRequest);
  }
}

/*
  Main server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.
  
  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {
  cout << "BasicServer: Parsing connection string" << endl;
  table_cache.init (storage_connection_string);

  cout << "BasicServer: Opening listener" << endl;
  http_listener listener {def_url};
  listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  listener.support(methods::PUT, &handle_put);
  listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop BasicServer." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "BasicServer closed" << endl;
}
