/*
  Sample unit tests for BasicServer
 */

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <cpprest/http_client.h>
#include <cpprest/json.h>

#include <pplx/pplxtasks.h>

#include <UnitTest++/UnitTest++.h>

using std::cerr;
using std::cout;
using std::endl;
using std::make_pair;
using std::pair;
using std::string;
using std::vector;

using web::http::http_headers;
using web::http::http_request;
using web::http::http_response;
using web::http::method;
using web::http::methods;
using web::http::status_code;
using web::http::status_codes;
using web::http::uri_builder;

using web::http::client::http_client;

using web::json::object;
using web::json::value;

constexpr const char* basic_url = "http://localhost:34568/";
constexpr const char* auth_url = "http://localhost:34570/";
constexpr const char* user_url = "http://localhost:34572/";
constexpr const char* push_url = "http://localhost:34574/";

const string create_table_op {"CreateTableAdmin"};
const string delete_table_op {"DeleteTableAdmin"};

const string read_entity_admin {"ReadEntityAdmin"};
const string update_entity_admin {"UpdateEntityAdmin"};
const string delete_entity_admin {"DeleteEntityAdmin"};

const string read_entity_auth {"ReadEntityAuth"};
const string update_entity_auth {"UpdateEntityAuth"};

const string get_read_token_op  {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};

const string get_update_data_op{ "GetUpdateData" };

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

// The two optional operations from Assignment 1
const string add_property_admin {"AddPropertyAdmin"};
const string update_property_admin {"UpdatePropertyAdmin"};

const string auth_table_name {"AuthTable"};
const string data_table_name {"DataTable"};

static constexpr const char* auth_table_partition {"Userid"};

/*
  Make an HTTP request, returning the status code and any JSON value in the body

  method: member of web::http::methods
  uri_string: uri of the request
  req_body: [optional] a json::value to be passed as the message body

  If the response has a body with Content-Type: application/json,
  the second part of the result is the json::value of the body.
  If the response does not have that Content-Type, the second part
  of the result is simply json::value {}.

  You're welcome to read this code but bear in mind: It's the single
  trickiest part of the sample code. You can just call it without
  attending to its internals, if you prefer.
 */
// Version with explicit third argument
pair<status_code,value> do_request (const method& http_method, const string& uri_string, const value& req_body) {
  http_request request {http_method};
  if (req_body != value {}) {
    http_headers& headers (request.headers());
    headers.add("Content-Type", "application/json");
    request.set_body(req_body);
  }

  status_code code;
  value resp_body;
  http_client client {uri_string};
  client.request (request)
    .then([&code](http_response response)
      {
        code = response.status_code();
        const http_headers& headers {response.headers()};
        auto content_type (headers.find("Content-Type"));
        if (content_type == headers.end() ||
            content_type->second != "application/json")
          return pplx::task<value> ([] { return value {};});
        else
          return response.extract_json();
      })
.then([&resp_body](value v) -> void
      {
        resp_body = v;
        return;
      })
    .wait();
  return make_pair(code, resp_body);
}

// Version that defaults third argument
pair<status_code,value> do_request (const method& http_method, const string& uri_string) {
  return do_request (http_method, uri_string, value {});
}

/*
  Utility to create a table

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
 */
int create_table (const string& addr, const string& table) {
  pair<status_code,value> result {do_request (methods::POST, addr + create_table_op + "/" + table)};
  return result.first;
}

/*
  Utility to compare two JSON objects

  This is an internal routine---you probably want to call compare_json_values().
 */
bool compare_json_objects (const object& expected_o, const object& actual_o) {
  CHECK_EQUAL (expected_o.size (), actual_o.size());
  if (expected_o.size() != actual_o.size())
    return false;

  bool result {true};
  for (auto& exp_prop : expected_o) {
    object::const_iterator act_prop {actual_o.find (exp_prop.first)};
    CHECK (actual_o.end () != act_prop);
    if (actual_o.end () == act_prop)
      result = false;
    else {
      CHECK_EQUAL (exp_prop.second, act_prop->second);
      if (exp_prop.second != act_prop->second)
        result = false;
    }
  }
  return result;
}

/*
  Utility to compare two JSON objects represented as values

  expected: json::value that was expected---must be an object
  actual: json::value that was actually returned---must be an object
*/
bool compare_json_values (const value& expected, const value& actual) {
  assert (expected.is_object());
  assert (actual.is_object());

  object expected_o {expected.as_object()};
  object actual_o {actual.as_object()};
  return compare_json_objects (expected_o, actual_o);
}

/*
  Utility to compare expected JSON array with actual

  exp: vector of objects, sorted by Partition/Row property 
    The routine will throw if exp is not sorted.
  actual: JSON array value of JSON objects
    The routine will throw if actual is not an array or if
    one or more values is not an object.

  Note the deliberate asymmetry of the how the two arguments are handled:

  exp is set up by the test, so we *require* it to be of the correct
  type (vector<object>) and to be sorted and throw if it is not.

  actual is returned by the database and may not be an array, may not
  be values, and may not be sorted by partition/row, so we have
  to check whether it has those characteristics and convert it 
  to a type comparable to exp.
*/
bool compare_json_arrays(const vector<object>& exp, const value& actual) {
  /*
    Check that expected argument really is sorted and
    that every value has Partion and Row properties.
    This is a precondition of this routine, so we throw
    if it is not met.
  */
  auto comp = [] (const object& a, const object& b) -> bool {
    return a.at("Partition").as_string()  <  b.at("Partition").as_string()
           ||
           (a.at("Partition").as_string() == b.at("Partition").as_string() &&
            a.at("Row").as_string()       <  b.at("Row").as_string()); 
  };
  if ( ! std::is_sorted(exp.begin(),
                         exp.end(),
                         comp))
    throw std::exception();

  // Check that actual is an array
  CHECK(actual.is_array());
  if ( ! actual.is_array())
    return false;
  web::json::array act_arr {actual.as_array()};

  // Check that the two arrays have same size
  CHECK_EQUAL(exp.size(), act_arr.size());
  if (exp.size() != act_arr.size())
    return false;

  // Check that all values in actual are objects
  bool all_objs {std::all_of(act_arr.begin(),
                             act_arr.end(),
                             [] (const value& v) { return v.is_object(); })};
  CHECK(all_objs);
  if ( ! all_objs)
    return false;

  // Convert all values in actual to objects
  vector<object> act_o {};
  auto make_object = [] (const value& v) -> object {
    return v.as_object();
  };
  std::transform (act_arr.begin(), act_arr.end(), std::back_inserter(act_o), make_object);

  /* 
     Ensure that the actual argument is sorted.
     Unlike exp, we cannot assume this argument is sorted,
     so we sort it.
   */
  std::sort(act_o.begin(), act_o.end(), comp);

  // Compare the sorted arrays
  bool eq {std::equal(exp.begin(), exp.end(), act_o.begin(), &compare_json_objects)};
  CHECK (eq);
  return eq;
}

/*
  Utility to create JSON object value from vector of properties
*/
value build_json_object (const vector<pair<string,string>>& properties) {
    value result {value::object ()};
    for (auto& prop : properties) {
      result[prop.first] = value::string(prop.second);
    }
    return result;
}

/*
  Utility to delete a table

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
 */
int delete_table (const string& addr, const string& table) {
  // SIGH--Note that REST SDK uses "methods::DEL", not "methods::DELETE"
  pair<status_code,value> result {
    do_request (methods::DEL,
                addr + delete_table_op + "/" + table)};
  return result.first;
}

/*
  Utility to put an entity with a single property

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
  prop: Name of the property
  pstring: Value of the property, as a string
 */
int put_entity(const string& addr, const string& table, const string& partition, const string& row, const string& prop, const string& pstring) {
  pair<status_code,value> result {
    do_request (methods::PUT,
                addr + update_entity_admin + "/" + table + "/" + partition + "/" + row,
                value::object (vector<pair<string,value>>
                               {make_pair(prop, value::string(pstring))}))};
  return result.first;
}

/*
  Utility to put an entity with multiple properties

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
  props: vector of string/value pairs representing the properties
 */
int put_entity(const string& addr, const string& table, const string& partition, const string& row,
              const vector<pair<string,value>>& props) {
  pair<status_code,value> result {
    do_request (methods::PUT,
               addr + update_entity_admin + "/" + table + "/" + partition + "/" + row,
               value::object (props))};
  return result.first;
}

/*
  Utility to delete an entity

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
 */
int delete_entity (const string& addr, const string& table, const string& partition, const string& row)  {
  // SIGH--Note that REST SDK uses "methods::DEL", not "methods::DELETE"
  pair<status_code,value> result {
    do_request (methods::DEL,
                addr + delete_entity_admin + "/" + table + "/" + partition + "/" + row)};
  return result.first;
}

/*
  Utility to get a token good for updating a specific entry
  from a specific table for one day.
 */
pair<status_code,string> get_update_token(const string& addr,  const string& userid, const string& password) {
  value pwd {build_json_object (vector<pair<string,string>> {make_pair("Password", password)})};
  pair<status_code,value> result {do_request (methods::GET,
                                              addr +
                                              get_update_token_op + "/" +
                                              userid,
                                              pwd
                                              )};
  cerr << "token " << result.second << endl;
  if (result.first != status_codes::OK) {
    cout << "Token is invalid.\n";
    return make_pair (result.first, "");
  } else {
    cout << "Token is successful and valid.\n";
    string token {result.second["token"].as_string()};
    return make_pair (result.first, token);
  }
}

/*
  Utility to get a token good for reading a specific entry
  from a specific table for one day.
*/
pair<status_code,string> get_read_token(const string& addr,  const string& userid, const string& password) {
  value pwd {build_json_object (vector<pair<string,string>> {make_pair("Password", password)})};
  pair<status_code,value> result {do_request (methods::GET,
                                              addr +
                                              get_read_token_op + "/" +
                                              userid,
                                              pwd
                                              )};
  cerr << "token " << result.second << endl;
  if (result.first != status_codes::OK) {
    cout << "Token is invalid.\n";
    return make_pair (result.first, "");
  } else {
    cout << "Token is successful and valid.\n";
     string token {result.second["token"].as_string()};
     return make_pair (result.first, token);
  }
}

/*
  A sample fixture that ensures TestTable exists, and
  at least has the entity Franklin,Aretha/USA
  with the property "Song": "RESPECT".

  The entity is deleted when the fixture shuts down
  but the table is left. See the comments in the code
  for the reason for this design.
 */

//Beginning of tests-------------------------------------------------------------------------------------------------------------------------------------------------------

class BasicFixture {
public:
  static constexpr const char* addr {"http://localhost:34568/"};
  static constexpr const char* table {"TestTable"};
  static constexpr const char* partition {"USA"};
  static constexpr const char* row {"Franklin,Aretha"};
  static constexpr const char* property {"Song"};
  static constexpr const char* prop_val {"RESPECT"};

public:
  BasicFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
      throw std::exception();
    }
    int put_result {put_entity (addr, table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
      throw std::exception();
    }
  }

  ~BasicFixture() {
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
      throw std::exception();
    }

    /*
      In traditional unit testing, we might delete the table after every test.

      However, in cloud NoSQL environments (Azure Tables, Amazon DynamoDB)
      creating and deleting tables are rate-limited operations. So we
      leave the table after each test but delete all its entities.
    */
    cout << "Skipping table delete" << endl;
    /*
      int del_result {delete_table(addr, table)};
      cerr << "delete result " << del_result << endl;
      if (del_result != status_codes::OK) {
        throw std::exception();
      }
    */
  }
};

SUITE(GET) {
  /*
    A test of GET all table entries

    Demonstrates use of new compare_json_arrays() function.
  */
  TEST_FIXTURE(BasicFixture, GetAll) {
    string partition {"Canada"};
    string row {"Katherines,The"};
    string property {"Home"};
    string prop_val {"Vancouver"};
    int put_result {put_entity (BasicFixture::addr, BasicFixture::table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    assert (put_result == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
                  string(BasicFixture::addr)
                  + read_entity_admin + "/"
                  + string(BasicFixture::table))};
    CHECK_EQUAL(status_codes::OK, result.first);

    value obj1 {
      value::object(vector<pair<string,value>> {
          make_pair(string("Partition"), value::string(partition)),
          make_pair(string("Row"), value::string(row)),
          make_pair(property, value::string(prop_val))
      })
    };
    value obj2 {
      value::object(vector<pair<string,value>> {
          make_pair(string("Partition"), value::string(BasicFixture::partition)),
          make_pair(string("Row"), value::string(BasicFixture::row)),
          make_pair(string(BasicFixture::property), value::string(BasicFixture::prop_val))
      })
    };

    vector<object> exp {
      obj1.as_object(),
      obj2.as_object()
    };
    bool same_objects = compare_json_arrays(exp, result.second);
    cout << "Are the objects the same? " << same_objects << endl;
    CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition, row));
  }
}

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                       ASSIGNMENT # 1                        ////
////               Start of Angel's code (Part 1)                ////
////                                                             ////
////        REQUIRED OPERATION 1: Get all entities from a        ////
////                      specific partition                     ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

SUITE(GetAllEntitiiesFromASpecificPartition) {
  class GetFixture {
  public:
    static constexpr const char* addr {"http://127.0.0.1:34568/"};
    static constexpr const char* table {"NewTable"};
    static constexpr const char* partition {"Franklin,Aretha"};
    static constexpr const char* row {"USA"};
    static constexpr const char* property {"Song"};
    static constexpr const char* prop_val {"RESPECT"};

  public:
    GetFixture() {
      int make_result {create_table(addr, table)};
      cerr << "create result " << make_result << endl;
      if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
         throw std::exception();
      }
      int put_result {put_entity (addr, table, partition, row, property, prop_val)};
      cerr << "put result " << put_result << endl;
      if (put_result != status_codes::OK) {
         throw std::exception();
      }
    }
    ~GetFixture() {
      int del_ent_result {delete_entity (addr, table, partition, row)};
      if (del_ent_result != status_codes::OK) {
  throw std::exception();
      }
      cout << "Skipping table delete" << endl;
    }
  };
   /*
    A test of GET for a nonexistent table
  */
  TEST_FIXTURE(GetFixture, GetNonExisTable) {
    cout << "non exis table\n";
    constexpr const char* invalidTable {"nonExisTable"};
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + invalidTable + "/"
      + GetFixture::partition + "/"
      + GetFixture::row)};
      
      CHECK_EQUAL(status_codes::NotFound, result.first);
    }

  // /*
  //   A test of GET of missing table
  // */
  TEST_FIXTURE(GetFixture, GetMissingTable) {
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      // + missingTable + "/"
      // + GetFixture::partition + "/"
      // + GetFixture::row
      )};
      
      CHECK_EQUAL(status_codes::BadRequest, result.first);
    }

  
  //   A test of GET for a missing partition
  
  TEST_FIXTURE(GetFixture, GetMissingPartition) {
    constexpr const char* invalidPartition {"brokenPartition"};
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + GetFixture::table + "/"
      + invalidPartition + "/"
      + GetFixture::row)};
      
      CHECK_EQUAL(status_codes::NotFound, result.first);
    }

  /*
    A test of GET for a missing row
  */
  TEST_FIXTURE(GetFixture, GetMissingRow) {
    constexpr const char* invalidRow {"brokenRow"};
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + GetFixture::table + "/"
      + GetFixture::partition + "/"
      + invalidRow
      )};
      
      CHECK_EQUAL(status_codes::NotFound, result.first);
    }

    TEST_FIXTURE(GetFixture, GetSpecificPartition) {
      cout << "================Testing a specific partition=================\n";

      string Part {"bubble"};
      string Row {"bubble1"};
      string Prop {"liquid"};
      string Val {"taro"};
      int PutResult {put_entity (GetFixture::addr, GetFixture::table, Part, Row, Prop, Val)};
      cerr << "put result " << PutResult << endl;
      assert (PutResult == status_codes::OK);

      string part_name = Part;
      pair<status_code, value> output {
        do_request(methods::GET, 
                    string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table) + "/" + part_name + string("/*")
                    )
      };
      CHECK_EQUAL(status_codes::OK, output.first);
      cout << "Result of first check: " << output.second.serialize() << endl;

      CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, Part, Row));
    }


    //test for bizarre table name
    TEST_FIXTURE(GetFixture, BadInputs) {
      constexpr const char* brokenTable {"!@4"};
      pair<status_code,value> result_t {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + brokenTable + "/"
      + GetFixture::partition + "/"
      + GetFixture::row
      )};

      CHECK_EQUAL(status_codes::NotFound, result_t.first);
    }
}

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                 End of Angel's code (Part 1)                ////
////                       ASSIGNMENT # 1                        ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                       ASSIGNMENT # 1                        ////
////               Start of Angel's code (Part 2)                ////
////                                                             ////
////   OPTIONAL ADDED OPERATIONS 1: Add the specified property   ////
////                      to all entities                        ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

SUITE(AddPropertyToEntities) {
  class PutFixture {
  public:
    static constexpr const char* addr {"http://127.0.0.1:34568/"};
    static constexpr const char* table {"NewTable1"};
    static constexpr const char* partition {"Trash"};
    static constexpr const char* row {"Canada"};
    static constexpr const char* property {"Song"};
    static constexpr const char* prop_val {"Bench"};

  public:
    PutFixture() {
      int make_result {create_table(addr, table)};
      cerr << "create result " << make_result << endl;
      if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
         throw std::exception();
      }
      int put_result {put_entity (addr, table, partition, row, property, prop_val)};
      cerr << "put result " << put_result << endl;
      if (put_result != status_codes::OK) {
         throw std::exception();
      }
    }
    ~PutFixture() {
      int del_ent_result {delete_entity (addr, table, partition, row)};
      if (del_ent_result != status_codes::OK) {
  throw std::exception();
      }
      cout << "Skipping table delete" << endl;
    }
  };

    TEST_FIXTURE(PutFixture, Add)
    {

      // First check - Add all properties with name "flavour" in an empty table
      string property_name {"flavour"};
      string property_value {"taro"};
      pair<status_code,value> result {
      
        do_request (methods::PUT,
                    string(PutFixture::addr) + add_property_admin + "/" + string(PutFixture::table),
                    value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
        
      };
      CHECK_EQUAL(status_codes::OK, result.first);

      // Read the table after first check    
      pair<status_code,value> get_table {

         do_request (methods::GET,
                    string(PutFixture::addr) + read_entity_admin + "/" + string(PutFixture::table))

      };
      CHECK_EQUAL(status_codes::OK, get_table.first);
      cout << "Result of first check: " << get_table.second.serialize() << endl << endl;

//      pair<status_code,value> put_nothing {
//
//        do_request (methods::PUT,
//                    string(PutFixture::addr) + update_entity_admin + "/" + string(PutFixture::table) + "/" + string("nothing1") + "/" + string("nothing2"))
//
//      };
//
//      cerr << "put result " << put_nothing.first << endl;
//      assert (put_nothing.first == status_codes::OK);
//
//
//      CHECK_EQUAL(status_codes::OK, delete_entity (PutFixture::addr, PutFixture::table, property_name, property_value));
    }
}

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                 End of Angel's code (Part 2)                ////
////                       ASSIGNMENT # 1                        ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                       ASSIGNMENT # 1                        ////
////                Start of Josh's code (Part 1)                ////
////                                                             ////
////     REQUIRED OPERATION 2: Get all entities containing all   ////
////                    specified properties                     ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

SUITE(GetAllEntitiesContainingAllSpecifiedProperties) {

  class GetFixture
  {
    public:
      static constexpr const char* addr {"http://127.0.0.1:34568/"};
      static constexpr const char* table {"StudentDatabase"};

    public:
      GetFixture() {
        int make_result {create_table(addr, table)};
        cerr << "create result " << make_result << endl;
        if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
           throw std::exception();
        }
      }

      ~GetFixture() {
        cout << "Skipping table delete" << endl;
      }
  };


/////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                         //
//                                        FIRST TEST                                       //
//                                                                                         //
/////////////////////////////////////////////////////////////////////////////////////////////

  /*
    A test of GET all entities containing all specified properties with special cases
      - Testing an empty table
      - Testing entries with no properties and property values
      - Testing entries with a property, but no property values
      - Testing when a table does not exist
      - Testing a missing table name
  */
  TEST_FIXTURE(GetFixture, SpecialCases)
  {
    cout << "============ New GET - FIRST TEST ===============\n";

    // First check - Get all entries with the property "Food" in an empty table
    string property_name {"Food"}; 
    string property_value {"*"};
    pair<status_code,value> result {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result.first);
    CHECK(result.second.is_array());
    CHECK_EQUAL(0, result.second.as_array().size());
    cout << "Result of first check: " << result.second.serialize() << endl;

    cout << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First entity - Entry with no property and property value
    string partition {"Kirkpatrick,Arthur"};
    string row {"UnitedKingdom"};
    pair<status_code,value> put_arthur {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition) + "/" + string(row))

    };

    cerr << "put result " << put_arthur.first << endl;
    assert (put_arthur.first == status_codes::OK);

    // Second entity - Entry with a property, but no property value
    string partition1 {"Ige,Adebola"};
    string row1 {"France"};
    string property1 {"Food"};
    int put_adebola {put_entity (GetFixture::addr, GetFixture::table, partition1, row1, property1, "")};
    cerr << "put result " << put_adebola << endl;
    assert (put_adebola == status_codes::OK);

    cout << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // Second check - Get all entries with the property "Food"
    pair<status_code,value> result2 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result2.first);
    CHECK(result2.second.is_array());
    CHECK_EQUAL(1, result2.second.as_array().size());
    cout << "Result of second check: " << result2.second.serialize() << endl;
    CHECK_EQUAL(string("[{\"") + "Food" + "\":\"" + "" + "\","
              + string("\"") + "Partition" + "\":\"" + partition1 + "\","
              + string("\"") + "Row" + "\":\"" + row1 + "\"}]", 
              result2.second.serialize());

    cout << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First error - The table does not exist
    pair<status_code,value> result3 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string("RandomTable"),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::NotFound, result3.first);

    // Second error - Missing table name
    pair<status_code,value> result4 {
    
      do_request (methods::GET,
                  string(GetFixture::addr))
      
    };
    CHECK_EQUAL(status_codes::BadRequest, result4.first);

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // End the test
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition1, row1));
  }

/////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                         //
//                                       SECOND TEST                                       //
//                                                                                         //
/////////////////////////////////////////////////////////////////////////////////////////////

  /*
    A test of GET all entities containing all specified properties
    - Testing entries with a single property and property value
    - Testing multiple entries
    - Testing entries with two properties
    - Testing with an input of one JSON body
    - Testing with an input of two JSON bodies
    - Testing when a table does not exist
    - Testing a missing table name
  */
  TEST_FIXTURE(GetFixture, GetEntitiesatProperty)
  {
    cout << "============ New GET - SECOND TEST ===============\n";

    // Zeroth entity
    string partition0 {"Singh,Angelina"};
    string row0 {"Canada"};
    string property0 {"Food"};
    string prop_val0 {"BubbleTea"};
    int put_angel {put_entity (GetFixture::addr, GetFixture::table, partition0, row0, property0, prop_val0)};
    cerr << "put result " << put_angel << endl;
    assert (put_angel == status_codes::OK);

    // First entity
    string partition {"Fernandez,Josh"};
    string row {"ThePhilippines"};
    string property {"Food"};
    string prop_val {"CreamofMushroom"};
    int put_josh {put_entity (GetFixture::addr, GetFixture::table, partition, row, property, prop_val)};
    cerr << "put result " << put_josh << endl;
    assert (put_josh == status_codes::OK);

    // Second entity
    string partition1 {"Song,Andrew"};
    string row1 {"SouthKorea"};
    string property1 {"Food"};
    string prop_val1 {"Sushi"};
    int put_woojin {put_entity (GetFixture::addr, GetFixture::table, partition1, row1, property1, prop_val1)};
    cerr << "put result " << put_woojin << endl;
    assert (put_woojin == status_codes::OK);

    // Third entity
    string partition2 {"Yu,Lawrence"};
    string row2 {"Taiwan"};
    string property2 {"Food"};
    string prop_val2 {"Pizza"};
    int put_lawrence {put_entity (GetFixture::addr, GetFixture::table, partition2, row2, property2, prop_val2)};
    cerr << "put result " << put_lawrence << endl;
    assert (put_lawrence == status_codes::OK);

    cout << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First check - Get all entries with the property "Food"
    string property_name {"Food"};
    string property_value {"*"};
    pair<status_code,value> result {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result.first);
    CHECK(result.second.is_array());
    CHECK_EQUAL(4, result.second.as_array().size());
    cout << "Result of first check: " << result.second.serialize() << endl << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // Fourth entity
    string partition3 {"Nguyen,Nhan"};
    string row3 {"China"};
    string property3 {"Age"};
    string prop_val3 {"30"};
    int put_nhan {put_entity (GetFixture::addr, GetFixture::table, partition3, row3, property3, prop_val3)};
    cerr << "put result " << put_nhan << endl;
    assert (put_nhan == status_codes::OK);

    // Fifth entity - Insert an entry with two or more properties
    string partition4 {"Magdurulan,Andrew"};
    string row4 {"Nigeria"};
    string property4a {"Age"};
    string prop_val4a {"19"};
    string property4b {"Food"};
    string prop_val4b {"FriedChicken"};

    pair<status_code,value> put_andrew {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition4) + "/" + string(row4),
                  value::object (vector<pair<string,value>>{make_pair(property4a, value::string(prop_val4a)),
                                                            make_pair(property4b, value::string(prop_val4b))}))

    };

    cerr << "put result " << put_andrew.first << endl;
    assert (put_andrew.first == status_codes::OK);

    cout << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // Second check - Get all entries with the property "Food"
    pair<status_code,value> result2 {
    
      do_request (methods::GET, string(GetFixture::addr) + read_entity_admin +
      "/" + string(GetFixture::table),
      value::object(vector<pair<string,value>>{make_pair(property_name,
      value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result2.first);
    CHECK(result2.second.is_array());
    CHECK_EQUAL(5, result2.second.as_array().size());
    cout << "Result of second check: " << result2.second.serialize() << endl << endl;

    // Third check - Get all entries with the property "Age"
    string property_name2 {"Age"};
    pair<status_code,value> result3 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name2, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result3.first);
    CHECK(result3.second.is_array());
    CHECK_EQUAL(2, result3.second.as_array().size());
    cout << "Result of third check: " << result3.second.serialize() << endl << endl;

    // Fourth check - Get all entries with both properties "Age" and "Food"
    pair<status_code,value> result4 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value)),
                                                           make_pair(property_name2, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result4.first);
    CHECK(result4.second.is_array());
    CHECK_EQUAL(1, result4.second.as_array().size());
    CHECK_EQUAL(string("[{\"") + "Age" + "\":\"" + prop_val4a + "\","
              + string("\"") + "Food" + "\":\"" + prop_val4b + "\","
              + string("\"") + "Partition" + "\":\"" + partition4 + "\","
              + string("\"") + "Row" + "\":\"" + row4 + "\"}]", 
              result4.second.serialize());
    cout << "Result of fourth check: " << result4.second.serialize() << endl << endl;

    // Fifth check - Get all entries with property "TravelDestination"; should be nothing
    string property_name3 {"TravelDestination"};
    pair<status_code,value> result5 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name3, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result5.first);
    CHECK(result5.second.is_array());
    CHECK_EQUAL(0, result5.second.as_array().size());
    cout << "Result of fifth check: " << result5.second.serialize() << endl << endl;

    // Sixth check - Get all entries with properties "Food" and "TravelDestination"; should be nothing
    pair<status_code,value> result6 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value)),
                                                           make_pair(property_name3, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result6.first);
    CHECK(result6.second.is_array());
    CHECK_EQUAL(0, result5.second.as_array().size());
    cout << "Result of sixth check: " << result6.second.serialize() << endl << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First error - The table does not exist
    pair<status_code,value> result7 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string("TeacherDatabase"),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::NotFound, result7.first);

    // Second error - Missing table name
    pair<status_code,value> result8 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/")
      
    };
    CHECK_EQUAL(status_codes::BadRequest, result8.first);

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // End the test
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition1, row1));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition2, row2));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition3, row3));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition4, row4));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition0, row0));
  }

/////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                         //
//                                        THIRD TEST                                       //
//                                                                                         //
/////////////////////////////////////////////////////////////////////////////////////////////

  /*
    A test of GET all entities containing all specified properties on entries with multiple properties
      - Testing multiple entries with two ore more properties
      - Testing with an input of one JSON body
      - Testing with an input of two JSON bodies
      - Testing with an input of various JSON bodies, but with no result
      - Testing partitions with two or more rows
  */
    TEST_FIXTURE(GetFixture, ExtendOperation)
  {
    cout << "============ New GET - THIRD TEST ===============\n";

    // First entity
    string partition1 {"Singh,Angelina"};
    string row1 {"Canada"};
    string property1a {"TravelDestination"};
    string prop_val1a {"London"};
    string property1b {"MusicArtist"};
    string prop_val1b {"DrDre"};
    string property1c {"Softdrink"};
    string prop_val1c {"Nestea"};

    pair<status_code,value> put_angel {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition1) + "/" + string(row1),
                  value::object (vector<pair<string,value>>{make_pair(property1a, value::string(prop_val1a)),
                                                            make_pair(property1b, value::string(prop_val1b)),
                                                            make_pair(property1c, value::string(prop_val1c))}))

    };

    cerr << "put result " << put_angel.first << endl;
    assert (put_angel.first == status_codes::OK);


    // Second entity
    string partition2 {"Fernandez,Josh"};
    string row2 {"ThePhilippines"};
    string property2a {"Softdrink"};
    string prop_val2a {"CanadaDry"};
    string property2b {"MusicArtist"};
    string prop_val2b {"Coldplay"};

    pair<status_code,value> put_josh {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition2) + "/" + string(row2),
                  value::object (vector<pair<string,value>>{make_pair(property2a, value::string(prop_val2a)),
                                                            make_pair(property2b, value::string(prop_val2b))}))

    };

    cerr << "put result " << put_josh.first << endl;
    assert (put_josh.first == status_codes::OK);

    // Third entity
    string partition3 {"Song,Andrew"};
    string row3 {"SouthKorea"};
    string property3a {"MusicArtist"};
    string prop_val3a {"JustinBieber"};
    string property3b {"FavoriteSong"};
    string prop_val3b {"LoveYourself"};

    pair<status_code,value> put_andrew {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition3) + "/" + string(row3),
                  value::object (vector<pair<string,value>>{make_pair(property3a, value::string(prop_val3a)),
                                                            make_pair(property3b, value::string(prop_val3b))}))

    };

    cerr << "put result " << put_andrew.first << endl;
    assert (put_andrew.first == status_codes::OK);

    // Fourth entity - Add another row to a partition
    string partition4 {"Singh,Angelina"};
    string row4 {"India"};
    string property4a {"TravelDestination"};
    string prop_val4a {"Manila"};
    string property4b {"Softdrink"};
    string prop_val4b {"Coca-Cola"};

    pair<status_code,value> put_angel_again {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition4) + "/" + string(row4),
                  value::object (vector<pair<string,value>>{make_pair(property4a, value::string(prop_val4a)),
                                                            make_pair(property4b, value::string(prop_val4b))}))

    };

    cerr << "put result " << put_angel_again.first << endl;
    assert (put_angel_again.first == status_codes::OK);

    cout << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First check - Get all entries with the property "MusicArtist"
    string property_name {"MusicArtist"};
    string property_value {"*"};
    pair<status_code,value> result {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result.first);
    CHECK(result.second.is_array());
    CHECK_EQUAL(3, result.second.as_array().size());
    cout << "Result of first check: " << result.second.serialize() << endl << endl;

    // Second check - Get all entries with the property "Softdrink"
    string property_name2 {"Softdrink"};
    pair<status_code,value> result2 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name2, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result2.first);
    CHECK(result2.second.is_array());
    CHECK_EQUAL(3, result2.second.as_array().size());
    cout << "Result of second check: " << result2.second.serialize() << endl << endl;

    // Third check - Get all entries with the property "MusicArtist" and "Softdrink"
    pair<status_code,value> result3 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value)),
                                                           make_pair(property_name2, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result3.first);
    CHECK(result3.second.is_array());
    CHECK_EQUAL(2, result3.second.as_array().size());
    cout << "Result of third check: " << result3.second.serialize() << endl << endl;

    // Fourth check - Get all entries with the property "MusicArtist", "Softdrink", and "TravelDestination"
    string property_name3 {"TravelDestination"};
    pair<status_code,value> result4 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value)),
                                                           make_pair(property_name2, value::string(property_value)),
                                                           make_pair(property_name3, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result4.first);
    CHECK(result4.second.is_array());
    CHECK_EQUAL(1, result4.second.as_array().size());
    cout << "Result of fourth check: " << result4.second.serialize() << endl << endl;

    // Fourth check - Get all entries with the property "MusicArtist", "FavoriteSong", and "TravelDestination"
    string property_name4 {"FavoriteSong"};
    pair<status_code,value> result5 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value)),
                                                           make_pair(property_name4, value::string(property_value)),
                                                           make_pair(property_name3, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result4.first);
    CHECK(result5.second.is_array());
    CHECK_EQUAL(0, result5.second.as_array().size());
    cout << "Result of fifth check: " << result5.second.serialize() << endl << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // End the test
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition1, row1));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition2, row2));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition3, row3));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition4, row4));
  }

}
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                  End of Josh's code (Part 1)                ////
////                       ASSIGNMENT # 1                        ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                       ASSIGNMENT # 1                        ////
////                Start of Josh's code (Part 2)                ////
////                                                             ////
////  OPTIONAL ADDED OPERATIONS 2: Update the specified property ////
////                      in all entities                        ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

SUITE(UpdateSpecifiedPropertyinAllEntities) {

  class GetFixture
  {
    public:
      static constexpr const char* addr {"http://127.0.0.1:34568/"};
      static constexpr const char* table {"MusicianDatabase"};

    public:
      GetFixture() {
        int make_result {create_table(addr, table)};
        cerr << "create result " << make_result << endl;
        if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
           throw std::exception();
        }
      }

      ~GetFixture() {
        cout << "Skipping table delete" << endl;
      }
  };


/////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                         //
//                                        FIRST TEST                                       //
//                                                                                         //
/////////////////////////////////////////////////////////////////////////////////////////////

  /*
    A test of UPDATE (PUT) the specified property in all entries
      - Testing an empty table
      - Testing entries with no properties and property values
      - Testing entries with a property, but no property values
      - Testing when a table does not exist
      - Testing with a missing JSON body
      - Testing a missing table name
  */
  TEST_FIXTURE(GetFixture, SpecialCases)
  {
    cout << "============ New PUT - FIRST TEST ===============\n";

    // First check - Update all properties with name "BandName" in an empty table
    string property_name {"BandName"};
    string property_value {"OneDirection"};
    pair<status_code,value> result {
    
      do_request (methods::PUT,
                  string(GetFixture::addr) + update_property_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result.first);

    // Read the table after first check    
    pair<status_code,value> get_table {

       do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table))

    };
    CHECK_EQUAL(status_codes::OK, get_table.first);
    cout << "Result of first check: " << get_table.second.serialize() << endl << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First entity - Entry with no property and property value
    string partition {"McCartney,Jesse"};
    string row {"UnitedKingdom"};
    pair<status_code,value> put_jesse {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition) + "/" + string(row))

    };

    cerr << "put result " << put_jesse.first << endl;
    assert (put_jesse.first == status_codes::OK);

    // Second entity - Entry with a property, but no property value
    string partition1 {"Brown,Chris"};
    string row1 {"UnitedStates"};
    string property1 {"BandName"};
    int put_chris_b {put_entity (GetFixture::addr, GetFixture::table, partition1, row1, property1, "")};
    cerr << "put result " << put_chris_b << endl;
    assert (put_chris_b == status_codes::OK);

    cout << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // Second check - Update all properties with name "BandName"
    pair<status_code,value> result2 {
    
      do_request (methods::PUT,
                  string(GetFixture::addr) + update_property_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result2.first);

    cout << endl;

    // Read the table after second check    
    pair<status_code,value> get_table1 {

       do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table))

    };
    CHECK_EQUAL(status_codes::OK, get_table1.first);
    cout << "Result of second check: " << get_table1.second.serialize() << endl << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First error - The table does not exist
    pair<status_code,value> result3 {
    
      do_request (methods::PUT,
                  string(GetFixture::addr) + update_property_admin + "/" + string("BandEquipmentDatabase"),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::NotFound, result3.first);

    // Second error - Missing table name
    /*
    pair<status_code,value> result4 {
    
      do_request (methods::PUT,
                  string(GetFixture::addr) + "UpdateProperty/")
      
    };
    CHECK_EQUAL(status_codes::NotFound, result4.first);
    */

    // Third error - Missing JSON body
    pair<status_code,value> result5 {
    
      do_request (methods::PUT,
                  string(GetFixture::addr) + update_property_admin + "/" + string(GetFixture::table))
      
    };
    CHECK_EQUAL(status_codes::BadRequest, result5.first);

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // End the test
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition1, row1));

  }

/////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                         //
//                                       SECOND TEST                                       //
//                                                                                         //
/////////////////////////////////////////////////////////////////////////////////////////////

  /*
    A test of UPDATE (PUT) the specified property in all entries
      - Testing entries with a single property and property value
      - Testing multiple entries
      - Testing entries with two or more properties
      - Testing when a table does not exist
      - Testing with a missing JSON body
      - Testing a missing table name
  */
  TEST_FIXTURE(GetFixture, GetEntitiesatProperty)
  {
    cout << "============ New PUT - SECOND TEST ===============\n";

    // Zeroth entity - Entry with one property
    string partition0 {"Jackson,Michael"};
    string row0 {"UnitedStates"};
    string property0 {"BandName"};
    string prop_val0 {"Jackson5"};
    int put_michael {put_entity (GetFixture::addr, GetFixture::table, partition0, row0, property0, prop_val0)};
    cerr << "put result " << put_michael << endl;
    assert (put_michael == status_codes::OK);

    // First entity - Entry with one property
    string partition {"Adkins,Adele"};
    string row {"France"};
    string property {"LatestAlbum"};
    string prop_val {"25"};
    int put_adele {put_entity (GetFixture::addr, GetFixture::table, partition, row, property, prop_val)};
    cerr << "put result " << put_adele << endl;
    assert (put_adele == status_codes::OK);

    // Second entity - Partition with multiple rows
    string partition1 {"Martin,Chris"};
    string row1a {"Australia"};
    string property1a {"BandName"};
    string prop_val1a {"Coldplay"};
    int put_chris_m {put_entity (GetFixture::addr, GetFixture::table, partition1, row1a, property1a, prop_val1a)};
    cerr << "put result " << put_chris_m << endl;
    assert (put_chris_m == status_codes::OK);

    string row1b {"India"};
    string property1b {"LatestAlbum"};
    string prop_val1b {"AHeadFullofDreams"};
    int put_chris_m_again {put_entity (GetFixture::addr, GetFixture::table, partition1, row1b, property1b, prop_val1b)};
    cerr << "put result " << put_chris_m_again << endl;
    assert (put_chris_m_again == status_codes::OK);

    // Third entity - Entry with multiple properties
    string partition2 {"Mars,Bruno"};
    string row2 {"ThePhilippines"};
    string property2a {"BandName"};
    string prop_val2a {"TheHooligans"};
    string property2b {"LatestAlbum"};
    string prop_val2b {"UnorthodoxJukebox"};
    pair<status_code,value> put_bruno {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition2) + "/" + string(row2),
                  value::object (vector<pair<string,value>>{make_pair(property2a, value::string(prop_val2a)),
                                                            make_pair(property2b, value::string(prop_val2b))}))

    };
    cerr << "put result " << put_bruno.first << endl;
    assert (put_bruno.first == status_codes::OK);

    // Fourth entity - Entry with multiple properties of the same name
    string partition3 {"Levine,Adam"};
    string row3 {"Mexico"};
    string property3 {"BandName"};
    string prop_val3a {"Maroon5"};
    string prop_val3b {"OneRepublic"};
    pair<status_code,value> put_adam {

      do_request (methods::PUT,
                  string(GetFixture::addr) + update_entity_admin + "/" + string(GetFixture::table) + "/" + string(partition3) + "/" + string(row3),
                  value::object (vector<pair<string,value>>{make_pair(property3, value::string(prop_val3a)),
                                                            make_pair(property3, value::string(prop_val3b))}))

    };
    cerr << "put result " << put_adam.first << endl;
    assert (put_adam.first == status_codes::OK);

    cout << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First check - Update all properties with name "BandName"
    string property_name {"BandName"};
    string property_value {"TheBeatles"};
    pair<status_code,value> result {
    
      do_request (methods::PUT,
                  string(GetFixture::addr) + update_property_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result.first);

    // Read the table after first check    
    pair<status_code,value> get_table {

       do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table))
    };
    CHECK_EQUAL(status_codes::OK, get_table.first);
    cout << "Result of first check: " << get_table.second.serialize() << endl << endl;

    // Second check - Update all properties with name "LatestAlbum"
    string property_name2 {"LatestAlbum"};
    string property_value2 {"SgtPepperLonelyHeartsClubBand"};
    pair<status_code,value> result2 {
    
      do_request (methods::PUT,
                  string(GetFixture::addr) + update_property_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name2, value::string(property_value2))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result2.first);

    // Read the table after second check    
    pair<status_code,value> get_table1 {

       do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table))
    };
    CHECK_EQUAL(status_codes::OK, get_table1.first);
    cout << "Result of second check: " << get_table1.second.serialize() << endl << endl;

    // Third check - Update all properties with name "DateFormed"; should be nothing
    string property_name3 {"DateFormed"};
    string property_value3 {"March16"};
    pair<status_code,value> result3 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + update_property_admin + "/" + string(GetFixture::table),
                  value::object(vector<pair<string,value>>{make_pair(property_name3, value::string(property_value3))}))
      
    };
    CHECK_EQUAL(status_codes::OK, result3.first);

    // Read the table after third check    
    pair<status_code,value> get_table2 {

       do_request (methods::GET,
                  string(GetFixture::addr) + read_entity_admin + "/" + string(GetFixture::table))
    };
    CHECK_EQUAL(status_codes::OK, get_table2.first);
    cout << "Result of third check: " << get_table2.second.serialize() << endl << endl;

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // First error - The table does not exist
    pair<status_code,value> result4 {
    
      do_request (methods::GET,
                  string(GetFixture::addr) + update_property_admin + "/" + string("CityGigsDatabase"),
                  value::object(vector<pair<string,value>>{make_pair(property_name, value::string(property_value))}))
      
    };
    CHECK_EQUAL(status_codes::NotFound, result4.first);

    // Third error - Missing JSON body
    pair<status_code,value> result5 {
    
      do_request (methods::PUT,
                  string(GetFixture::addr) + update_property_admin + "/" + string(GetFixture::table))
      
    };
    CHECK_EQUAL(status_codes::BadRequest, result5.first);

    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////

    // End the test
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition1, row1a));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition1, row1b));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition2, row2));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition3, row3));
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition0, row0));
  }

}
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                  End of Josh's code (Part 2)                ////
////                       ASSIGNMENT # 1                        ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                       ASSIGNMENT # 2                        ////
////                                                             ////
////      REQUIRED OPERATIONS: Read and update entities with     ////
////                       authorization                         ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

class AuthFixture {
public:
  static constexpr const char* addr {"http://localhost:34568/"};
  static constexpr const char* auth_addr {"http://localhost:34570/"};
  static constexpr const char* userid {"user"}; // AuthTable row key
  static constexpr const char* user_pwd {"user"}; // AuthTable Property 1: Password Value
  static constexpr const char* auth_table {"AuthTable"};
  static constexpr const char* auth_table_partition {"Userid"}; // AuthTable partition key
  static constexpr const char* auth_pwd_prop {"Password"}; // AuthTable Property 1: Password Name
  static constexpr const char* table {"DataTable"};
  static constexpr const char* partition {"USA"}; // AuthTable Property 2: Data Partition Value
  static constexpr const char* row {"Franklin,Aretha"}; // AuthTable Property 3: Data Row Value
  static constexpr const char* property {"Song"};
  static constexpr const char* prop_val {"RESPECT"};

public:
  AuthFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
      throw std::exception();
    }
    int put_result {put_entity (addr, table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
      throw std::exception();
    }

    // Create an Authorization Table
    int make_authtable {create_table(addr, auth_table)};
    cerr << "create result " << make_authtable << endl;
    if (make_authtable != status_codes::Created && make_authtable != status_codes::Accepted) {
      throw std::exception();
    }

    /*
      Ensure userid and password in system
      According to Lawrence, every user needs the following three properties: (For example)
          Password: user (user_pwd)
          Data Partition: USA (partition)
          Data Row: Franklin,Aretha (row)
    */
    pair<status_code,value> add_auth {

      do_request (methods::PUT,
                  addr + update_entity_admin + "/" + auth_table + "/" + auth_table_partition + "/" + userid,
                  value::object (vector<pair<string,value>>{make_pair("Password", value::string(user_pwd)),
                                                            make_pair("DataPartition", value::string(partition)),
                                                            make_pair("DataRow", value::string(row))}))

    };
    cerr << "user auth table insertion result " << add_auth.first << endl;
    if (add_auth.first != status_codes::OK) {
      throw std::exception();
    }

    // Initial check - Get the contents of the authorization table
    cout << "============ Initial GET contents of AuthTable (Read) ===============\n";

    pair<status_code,value> init_result {
    
      do_request (methods::GET,
                  string(addr) + read_entity_admin + "/" + auth_table )
      
    };
    CHECK_EQUAL(status_codes::OK, init_result.first);
    cout << "Result of initial check: " << init_result.second.serialize() << endl;

    cout << endl;

  }

  ~AuthFixture() {
    // Delete Userid/user from AuthTable
    int del_auth_ent_result {delete_entity (addr, auth_table, auth_table_partition, userid)};
    if (del_auth_ent_result != status_codes::OK) {
      cout << "Deleting Userid/user from AuthTable was unsuccessful.\n";
      throw std::exception();
    }

    // Delete USA/Franklin,Aretha from DataTable
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
      cout << "Deleting USA/Franklin/Aretha from DataTable was unsuccessful.\n";
      throw std::exception();
    }
  }
};

//////////////////////////////////////////////
//                                          //
//               ASSIGNMENT # 2             //
//     Checking the basics of AuthTable     //
//                                          //
//////////////////////////////////////////////

SUITE(AUTH_TABLE) {

  TEST_FIXTURE(AuthFixture, GetToken) {
    cout << "============ AuthTable Test 1: Basic Test ===============\n";

    cout << "Requesting read token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    cout << "Requesting update token" << endl;
    pair<status_code,string> token_res2 {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res2.first << endl;
    CHECK_EQUAL (token_res2.first, status_codes::OK);
  }

  TEST_FIXTURE(AuthFixture, WeirdPassword) {
    cout << "============ AuthTable Test 2: Weird Password in getting a token ===============\n";

    cout << "Requesting read token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       "撒旦法杀手工会")
    };
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::BadRequest);

    cout << "Requesting update token" << endl;
    pair<status_code,string> token_res2 {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       "撒旦法杀手工会")
    };
    cout << "Token response " << token_res2.first << endl;
    CHECK_EQUAL (token_res2.first, status_codes::BadRequest);
  }

  //This next test reflects both getting a read token and an update token
  TEST_FIXTURE(AuthFixture, BadandNFRequests) {
    cout << "============ AuthTable Test 3: Bad and Not Found Requests ===============\n";

    // Missing username
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res1 {
      get_read_token(AuthFixture::auth_addr,
                       "",
                       AuthFixture::user_pwd)
    };
    cout << "First token response " << token_res1.first << endl;
    CHECK_EQUAL (token_res1.first, status_codes::BadRequest);

    // Missing password
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res2 {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       "")
    };
    cout << "Second token response " << token_res2.first << endl;
    CHECK_EQUAL (token_res2.first, status_codes::BadRequest);

    // No such thing as property "Password"
    cout << "Requesting token" << endl;
    pair<status_code,value> token_res3 {

      do_request (methods::GET,
                  string(AuthFixture::auth_addr) + get_read_token_op + "/" + string(AuthFixture::userid),
                  value::object(vector<pair<string,value>>{make_pair("DataPartition", value::string(AuthFixture::partition))}))

    };
    cout << "Third token response " << token_res3.first << endl;
    CHECK_EQUAL (token_res3.first, status_codes::BadRequest);

    // Two or more properties
    cout << "Requesting token" << endl;
    pair<status_code,value> token_res4 {

      do_request (methods::GET,
                  string(AuthFixture::auth_addr) + get_read_token_op + "/" + string(AuthFixture::userid),
                  value::object(vector<pair<string,value>>{make_pair("Password", value::string(AuthFixture::user_pwd)),
                                                           make_pair("DataPartition", value::string(AuthFixture::partition)),
                                                           make_pair("DataRow", value::string(AuthFixture::row))}))

    };
    cout << "Fourth token response " << token_res4.first << endl;
    CHECK_EQUAL (token_res4.first, status_codes::BadRequest);

    // Username does not match
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res5 {
      get_read_token(AuthFixture::auth_addr,
                       "random_user",
                       AuthFixture::user_pwd)
    };
    cout << "Fifth token response " << token_res5.first << endl;
    CHECK_EQUAL (token_res5.first, status_codes::NotFound);

    // Password does not match
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res6 {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       "random_password")
    };
    cout << "Sixth token response " << token_res6.first << endl;
    CHECK_EQUAL (token_res6.first, status_codes::NotFound);
  }

  TEST_FIXTURE(AuthFixture, EmptyAuthTable) {
    cout << "============ AuthTable Test 4: Empty Authorization Table ===============\n";

    // Delete Userid/user from AuthTable
    int del_auth_ent_result {delete_entity (AuthFixture::addr, AuthFixture::auth_table, AuthFixture::auth_table_partition, AuthFixture::userid)};
    if (del_auth_ent_result != status_codes::OK) {
      cout << "Deleting Userid/user from AuthTable was unsuccessful.\n";
      throw std::exception();
    }

    // Check - Make sure there is nothing in the authorization table
    cout << "---Getting contents of AuthTable---\n";
    pair<status_code,value> init_result {
    
      do_request (methods::GET,
                  string(AuthFixture::addr) + read_entity_admin + "/" + AuthFixture::auth_table)
      
    };
    CHECK_EQUAL(status_codes::OK, init_result.first);
    cout << "Result of initial check: " << init_result.second.serialize() << endl;

    // Start the test
    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::NotFound);

    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of first check: " << result.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, result.first);

    // Add Userid/user to AuthTable before ending the test
    pair<status_code,value> add_auth {

      do_request (methods::PUT,
                  addr + update_entity_admin + "/" + auth_table + "/" + auth_table_partition + "/" + userid,
                  value::object (vector<pair<string,value>>{make_pair("Password", value::string(user_pwd)),
                                                            make_pair("DataPartition", value::string(partition)),
                                                            make_pair("DataRow", value::string(row))}))

    };
    cerr << "user auth table insertion result " << add_auth.first << endl;
    if (add_auth.first != status_codes::OK) {
      throw std::exception();
    }
  }
}

//////////////////////////////////////////////
//                                          //
//      ASSIGNMENT # 2: Lawrence's code     //
//                                          //
//////////////////////////////////////////////

// (Lawrence) Required operation 2: Update entity with authorization
SUITE(UPDATE_AUTH) {
  TEST_FIXTURE(AuthFixture, PutAuth) {
    cout << "============ UpdateAuth Test 1: Basic Test ===============\n";

    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of first check: " << result.first << endl;
    CHECK_EQUAL(status_codes::OK, result.first);
  }

  TEST_FIXTURE(AuthFixture, WrongToken) {
    cout << "============ UpdateAuth Test 2: Read Token instead of Update Token  ===============\n";

    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of second check: " << result.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, result.first);
  }

  TEST_FIXTURE(AuthFixture, MissingParameters) {
    cout << "============ UpdateAuth Test 3: Missing and Wrong Parameters  ===============\n";

    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Missing method
    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  //+ update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of second check: " << result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, result.first);

    // Missing table
    pair<status_code,value> result2 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  //+ AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of third check: " << result2.first << endl;
    CHECK_EQUAL(status_codes::NotFound, result2.first);

    // Missing token
    pair<status_code,value> result3 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  //+ token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of fourth check: " << result3.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, result3.first);

    // Missing partition
    pair<status_code,value> result4 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  //+ AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of fifth check: " << result4.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, result4.first);

    // Missing row
    pair<status_code,value> result5 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/",
                  //+ AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of sixth check: " << result5.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, result5.first);

    // Missing JSON object
    pair<status_code,value> result6 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row
                  //value::object (vector<pair<string,value>>
                  //                 {make_pair(added_prop.first,
                  //                            value::string(added_prop.second))})
                  )};
    cout << "Result of seventh check: " << result6.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, result6.first);

    // Wrong table
    pair<status_code,value> result7 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + "RandomTable" + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of eighth check: " << result7.first << endl;
    CHECK_EQUAL(status_codes::NotFound, result7.first);

    // Wrong partition
    pair<status_code,value> result8 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + "another_userid" + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of ninth check: " << result8.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, result8.first);

    // Wrong row
    pair<status_code,value> result9 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + "another_user",
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of tenth check: " << result9.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, result9.first);

    // Wrong property value
    pair<status_code,value> result10 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string("another_password"))})
                  )};
    cout << "Result of eleventh check: " << result10.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, result10.first);
  }
}

//////////////////////////////////////////////
//                                          //
//       ASSIGNMENT # 2: Andrew's code      //
//                                          //
//////////////////////////////////////////////

// (Andrew) Required operation 1: Read entity with authorization
SUITE(READ_AUTH){
  TEST_FIXTURE(AuthFixture, ReadAuth){
    cout << "============= ReadAuth Test 1: Basic Test =============" << endl;
    // REQUEST READ TOKEN
    cout << "Requesting token..." << endl;
    pair<status_code,string> read_token_res {
    get_read_token(AuthFixture::auth_addr,
                    AuthFixture::userid,
                    AuthFixture::user_pwd)};
    cout << "Token response " << read_token_res.first << endl;
    CHECK_EQUAL (read_token_res.first, status_codes::OK);

    pair<status_code,value> read_ret_res {
    do_request (methods::GET,
                string(AuthFixture::addr)
                + read_entity_auth + "/"                  // Command
                + AuthFixture::table + "/"                // Table Name
                + read_token_res.second + "/"             // Token
                + AuthFixture::partition + "/"            // Partition
                + AuthFixture::row)};                     // Row
    CHECK_EQUAL (status_codes::OK, read_ret_res.first);
    cout << "Result of check: " << read_ret_res.second.serialize() << endl;

    // Read the result
    value read_expect {
    build_json_object (
                        vector<pair<string,string>> {
                        //added_prop,
                        make_pair(string(AuthFixture::property), 
                        string(AuthFixture::prop_val))}
    )};

    cout << AuthFixture::property << endl;
    compare_json_values (read_expect, read_ret_res.second);
    cout << "Read authorized and successful. Entity returned as JSON object.\n";
  }

 TEST_FIXTURE(AuthFixture, LessThanFourParams) {
   cout << "============= ReadAuth Test 2: Test for Less Than Four Parametres =============" << endl;
   cout << "Requesting token..." << endl;
   pair<status_code,string> token_res {
   get_read_token(AuthFixture::auth_addr,
               AuthFixture::userid,
               AuthFixture::user_pwd)};
   cout << "Token response " << token_res.first << endl;
   CHECK_EQUAL (token_res.first, status_codes::OK);
   pair<status_code,value> result {
   do_request (methods::GET,
               string(AuthFixture::addr)
               + read_entity_auth + "/"
               + AuthFixture::table + "/"
               //+ token_res.second + "/"
               + AuthFixture::partition + "/"
               + AuthFixture::row
               )};
   CHECK_EQUAL(status_codes::BadRequest, result.first);
   cout << "Error Code: " << result.first << endl;
   cout << "Result of check: " << result.second.serialize() << endl;
   pair<status_code,value> result2 {
   do_request (methods::GET,
               string(AuthFixture::addr)
               + read_entity_auth + "/"
               + AuthFixture::table + "/"
               + token_res.second + "/"
               //+ AuthFixture::partition + "/"
               + AuthFixture::row
               )};
   CHECK_EQUAL(status_codes::BadRequest, result2.first);
   cout << "Error Code: " << result2.first << endl;
   cout << "Result of check: " << result2.second.serialize() << endl;
   pair<status_code,value> result3 {
   do_request (methods::GET,
               string(AuthFixture::addr)
               + read_entity_auth + "/"
               + AuthFixture::table + "/"
               + token_res.second + "/"
               + AuthFixture::partition
               //+ AuthFixture::row
               )};
   CHECK_EQUAL(status_codes::BadRequest, result3.first);
   cout << "Error Code: " << result3.first << endl;
   cout << "Result of check: " << result3.second.serialize() << endl;
 }

 TEST_FIXTURE(AuthFixture, UnauthToken) {
  cout << "============= ReadAuth Test 3: Test for Unauthorized Token =============" << endl;

  string id{"invalidUserID"};
  string pwd{"invalidUserPassword"};

  cout << "Requesting token..." << endl;
  pair<status_code,string> token_res {
  get_read_token(AuthFixture::auth_addr,
                  id,
                  AuthFixture::user_pwd)};
  cout << "Token response: " << token_res.first << endl;
  CHECK_EQUAL (token_res.first, status_codes::NotFound);

  cout << "Requesting token..." << endl;
  pair<status_code,string> token_res2 {
  get_read_token(AuthFixture::auth_addr,
                  AuthFixture::userid,
                  pwd)};
  cout << "Token response: " << token_res2.first << endl;
  CHECK_EQUAL (token_res2.first, status_codes::NotFound);
}
  
 TEST_FIXTURE(AuthFixture, NoEntity) {
     cout << "============= ReadAuth Test 4: Test for No Entity =============" << endl;
     cout << "Requesting token..." << endl;
     pair<status_code,string> token_res {
     get_read_token(AuthFixture::auth_addr,
                                 AuthFixture::userid,
                                 AuthFixture::user_pwd)};
     cout << "Token response " << token_res.first << endl;
     CHECK_EQUAL (token_res.first, status_codes::OK);
     pair<status_code,value> result {
     do_request (methods::GET,
     string(AuthFixture::addr)
             //+ read_entity_auth + "/"
             + AuthFixture::table + "/"
             + token_res.second + "/"
             + AuthFixture::partition + "/"
             + AuthFixture::row
             )};
     CHECK_EQUAL(status_codes::NotFound, result.first);
     cout << "Error Code: " << result.first << endl;
     cout << "Result of check: " << result.second.serialize() << endl;
 }

  TEST_FIXTURE(AuthFixture, NoTable) {
      cout << "============= ReadAuth Test 4: Test for No Table =============" << endl;
      cout << "Requesting token..." << endl;
      pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                                  AuthFixture::userid,
                                  AuthFixture::user_pwd)};
      cout << "Token response " << token_res.first << endl;
      CHECK_EQUAL (token_res.first, status_codes::OK);
      pair<status_code,value> result {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  //+ "RandomTable" + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row
                  )};
      CHECK_EQUAL(status_codes::NotFound, result.first);
      cout << "Error Code: " << result.first << endl;
      cout << "Result of check: " << result.second.serialize() << endl;
  }
}

//////////////////////////////////////////////
//                                          //
//               ASSIGNMENT # 2             //
//       Combining both read and update     //
//                 operations               //
//                                          //
//////////////////////////////////////////////
SUITE(EXTEND_OPERATION) {
  TEST_FIXTURE(AuthFixture, ExtendOperation) {
    cout << "============ Extend Operation Test: The Complete Test  ===============\n";

    // Add another entity to DataTable
    string partition {"Canada"};
    string row {"JustinBieber"};
    string property {"Song"};
    string prop_val {"BeautyAndABeat"};
    int put_justin {put_entity (AuthFixture::addr, AuthFixture::table, partition, row, property, prop_val)};
    cerr << "put result " << put_justin << endl;
    assert (put_justin == status_codes::OK);

    // Add another entity to AuthTable
    string userid {"andrew"};
    string user_pwd {"song"};
    pair<status_code,value> put_andrew {

      do_request (methods::PUT,
                  string(AuthFixture::addr) + update_entity_admin + "/" + string(AuthFixture::auth_table) + "/" + string(AuthFixture::auth_table_partition) + "/" + string(userid),
                  value::object (vector<pair<string,value>>{make_pair("Password", value::string(user_pwd)),
                                                            make_pair("DataPartition", value::string(partition)),
                                                            make_pair("DataRow", value::string(row))}))

    };
    cerr << "user auth table insertion result " << put_andrew.first << endl;
    assert (put_andrew.first == status_codes::OK);

    // Start the test
    pair<string,string> added_prop {make_pair(string("food"),string("chicken"))};

    // Requesting tokens for both entries
    cout << "Requesting first token" << endl;
    pair<status_code,string> token_res1 {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "First token response " << token_res1.first << endl;
    CHECK_EQUAL (token_res1.first, status_codes::OK);

    cout << "Requesting second token" << endl;
    pair<status_code,string> token_res2 {
      get_update_token(AuthFixture::auth_addr,
                       userid,
                       user_pwd)};
    cout << "Second token response " << token_res2.first << endl;
    CHECK_EQUAL (token_res2.first, status_codes::OK);

    // Putting the property for both entries, authorized
    pair<status_code,value> result1 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res1.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of first PUT check: " << result1.first << endl;
    CHECK_EQUAL(status_codes::OK, result1.first);

    pair<status_code,value> result2 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res2.second + "/"
                  + partition + "/"
                  + row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    cout << "Result of second PUT check: " << result1.first << endl;
    CHECK_EQUAL(status_codes::OK, result1.first);

    // Getting both entries, authorized
    pair<status_code,value> read_ret_res1 {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"                  // Command
                  + AuthFixture::table + "/"                // Table Name
                  + token_res1.second + "/"                 // Token
                  + AuthFixture::partition + "/"            // Partition
                  + AuthFixture::row)};                     // Row
    CHECK_EQUAL (status_codes::OK, read_ret_res1.first);
    cout << "Result of first GET check: " << read_ret_res1.second.serialize() << endl;

    pair<status_code,value> read_ret_res2 {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"                  // Command
                  + AuthFixture::table + "/"                // Table Name
                  + token_res2.second + "/"                 // Token
                  + partition + "/"                         // Partition
                  + row)};                                  // Row
    CHECK_EQUAL (status_codes::OK, read_ret_res2.first);
    cout << "Result of first GET check: " << read_ret_res2.second.serialize() << endl;

    // Get the contents of the authorization table
    cout << "============ GET contents of AuthTable ===============\n";

    pair<status_code,value> read_auth_table {
    
      do_request (methods::GET,
                  string(AuthFixture::addr) + read_entity_admin + "/" + AuthFixture::auth_table )
      
    };
    CHECK_EQUAL(status_codes::OK, read_auth_table.first);
    cout << "Contents of authorization table: " << read_auth_table.second.serialize() << endl;

    // Get the contents of the data table
    cout << "============ GET contents of DataTable ===============\n";

    pair<status_code,value> read_data_table {
    
      do_request (methods::GET,
                  string(AuthFixture::addr) + read_entity_admin + "/" + AuthFixture::table )
      
    };
    CHECK_EQUAL(status_codes::OK, read_data_table.first);
    cout << "Contents of data table: " << read_data_table.second.serialize() << endl;

    // End test: Delete andrew/song from AuthTable
    CHECK_EQUAL(status_codes::OK, delete_entity (AuthFixture::addr, AuthFixture::auth_table, AuthFixture::auth_table_partition, userid));

    // End test: Delete USA/Franklin,Aretha from DataTable
    CHECK_EQUAL(status_codes::OK, delete_entity (AuthFixture::addr, AuthFixture::table, partition, row));

    cout << endl;
  }
}

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////                                                             ////
////                       ASSIGNMENT # 3                        ////
////                                                             ////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////


// SignOn and SignOff tests
SUITE(SignOnAndOff)
{
  class UserFixture
  {
  public:
     
      static constexpr const char* user1_id {"Lawrence"};
      static constexpr const char* user1_password {"Yu"};
      static constexpr const char* user1_DataPartition {"Canada"};
      static constexpr const char* user1_DataRow {"Yu,Lawrence"};

      static constexpr const char* user2_id {"Josh"};
      static constexpr const char* user2_password {"Fernandez"};
      static constexpr const char* user2_DataPartition {"ThePhilippines"};
      static constexpr const char* user2_DataRow {"Fernandez,Josh"};

      static constexpr const char* user3_id {"Andrew"};
      static constexpr const char* user3_password {"Song"};
      static constexpr const char* user3_DataPartition {"Korea"};
      static constexpr const char* user3_DataRow {"Song,Andrew"};

      static constexpr const char* user4_id {"Angel"};
      static constexpr const char* user4_password {"Singh"};
      static constexpr const char* user4_DataPartition {"Korea"};
      static constexpr const char* user4_DataRow {"Singh,Angel"};
  public:
    UserFixture()
    {  
      //Initialize AuthTable users
      pair<status_code,value> put_result {

        do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string(user1_id),
                    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user1_password)),
                                                              make_pair("DataPartition", value::string(user1_DataPartition)),
                                                              make_pair("DataRow", value::string(user1_DataRow))}))

      };
      //cerr << "user1 auth table insertion result " << put_result.first << endl;
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }


      //Initialize DataTable users
      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + user1_DataPartition + "/" + user1_DataRow,
                    value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                              make_pair("Status", value::string("")),
                                                              make_pair("Updates", value::string(""))}));

      //cerr << "user1 DataTable insertion result " << put_result.first << endl;
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }
    }

    ~UserFixture()
    {
      //Delete AuthTable users
      int del_ent_result {delete_entity (basic_url, auth_table_name, auth_table_partition, user1_id)};
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }

      //Delete DataTable users
      del_ent_result = delete_entity (basic_url, data_table_name, user1_DataPartition, user1_DataRow);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
    }
};


  TEST_FIXTURE(UserFixture, SuccessfulSignOnAndOff)
  {
    pair<status_code,value> put_result = do_request (methods::PUT,
                  string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string(user2_id),
                  value::object (vector<pair<string,value>>{make_pair("Password", value::string(user2_password)),
                                                            make_pair("DataPartition", value::string(user2_DataPartition)),
                                                            make_pair("DataRow", value::string(user2_DataRow))}));
    if (put_result.first != status_codes::OK) {
      throw std::exception();
    }

    put_result = do_request (methods::PUT,
                  string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + user2_DataPartition + "/" + user2_DataRow,
                  value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                            make_pair("Status", value::string("")),
                                                            make_pair("Updates", value::string(""))}));
    if (put_result.first != status_codes::OK) {
      throw std::exception();
    }

    //Normal sign on
    pair<status_code,value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(UserFixture::user1_password))}));
    cout << "SuccessfulSignOnAndOff User1 SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    //already signed in
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(UserFixture::user1_password))}));
    cout << "SuccessfulSignOnAndOff User1 SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    //Normal sign on user2
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user2_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user2_password))}));
    cout << "SuccessfulSignOnAndOff User2 SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    //already signed in and wrong password
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string("sfds"))}));
    cout << "SuccessfulSignOnAndOff User1 SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_on_result.first);

    //Normal sign off user2
    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user2_id);
    cout << "SuccessfulSignOnAndOff User2 SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);

    //Normal sign off
    sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + UserFixture::user1_id);
    cout << "SuccessfulSignOnAndOff User1 SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);

    CHECK_EQUAL(status_codes::OK, delete_entity (basic_url, auth_table_name, auth_table_partition, user2_id));
    CHECK_EQUAL(status_codes::OK, delete_entity (basic_url, data_table_name, user2_DataPartition, user2_DataRow));
  }

  TEST_FIXTURE(UserFixture, SignOnNotAlphabetical)
  {
    pair<status_code,value>
    put_result = do_request (methods::PUT,
                  string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string("12345"),
                  value::object (vector<pair<string,value>>{make_pair("Password", value::string("a")),
                                                            make_pair("DataPartition", value::string("b")),
                                                            make_pair("DataRow", value::string("c"))}));
    if (put_result.first != status_codes::OK) {
      throw std::exception();
    }

    put_result = do_request (methods::PUT,
                  string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + "b" + "/" + "c",
                  value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                            make_pair("Status", value::string("")),
                                                            make_pair("Updates", value::string(""))}));
    if (put_result.first != status_codes::OK) {
      throw std::exception();
    }

    //userid containing numbers
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + "12345", 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string("a"))}));
    cout << "SignOnNotAlphabetical SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_on_result.first);

    put_result = do_request (methods::PUT,
                  string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string("@#?"),
                  value::object (vector<pair<string,value>>{make_pair("Password", value::string("d")),
                                                            make_pair("DataPartition", value::string("e")),
                                                            make_pair("DataRow", value::string("f"))}));
    if (put_result.first != status_codes::OK) {
      throw std::exception();
    }
    put_result = do_request (methods::PUT,
                  string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + "e" + "/" + "f",
                  value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                            make_pair("Status", value::string("")),
                                                            make_pair("Updates", value::string(""))}));
    if (put_result.first != status_codes::OK) {
      throw std::exception();
    }

    //userid containg symbols
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + "@#?", 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string("d"))}));
    cout << "SignOnNotAlphabetical SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_on_result.first);


    CHECK_EQUAL(status_codes::OK, delete_entity (basic_url, auth_table_name, auth_table_partition, "12345"));
    CHECK_EQUAL(status_codes::OK, delete_entity (basic_url, data_table_name, "b", "c"));
    CHECK_EQUAL(status_codes::OK, delete_entity (basic_url, auth_table_name, auth_table_partition, "@#?"));
    CHECK_EQUAL(status_codes::OK, delete_entity (basic_url, data_table_name, "e", "f"));
  }

  TEST_FIXTURE(UserFixture, SignOnPropertiesSizeNotEqualOne)
  {
    //propertie size of 2
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string(UserFixture::user1_password)),
                                                  make_pair("3", value::string(UserFixture::user1_password))}));
    cout << "SignOnPropertiesSizeNotEqualOne SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_on_result.first);
  }

  TEST_FIXTURE(UserFixture, SignOnEmptyPassword)
  {
    //empty password
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string(""))}));
    cout << "SignOnEmptyPassword SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_on_result.first);
  }

  TEST_FIXTURE(UserFixture, SignOnNonASCII7Password)
  {
    //password with foreign characters
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string("啊手动阀手动阀"))}));
    cout << "SignOnNonASCII7Password SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_on_result.first);
  }

  TEST_FIXTURE(UserFixture, SignOnUserDoesNotExist)
  {
    //non existing user
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + "John", 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string("sfasd"))}));
    cout << "SignOnUserDoesNotExist SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_on_result.first);


    pair<status_code,value>
    put_result = do_request (methods::PUT,
                  string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string("Joe"),
                  value::object (vector<pair<string,value>>{make_pair("Password", value::string("aaa")),
                                                            make_pair("DataPartition", value::string("bbb")),
                                                            make_pair("DataRow", value::string("ccc"))}));
    if (put_result.first != status_codes::OK) {
      throw std::exception();
    }

    //user exists in auth table but not in data table
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + "Joe", 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string("aaa"))}));
    cout << "SignOnUserDoesNotExist SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_on_result.first);

    CHECK_EQUAL(status_codes::OK, delete_entity (basic_url, auth_table_name, auth_table_partition, "Joe"));
  }



  TEST_FIXTURE(UserFixture, SignOffUserDoesNotHaveAnActiveSession)
  {
    //sign off user with no active session
    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + UserFixture::user1_id);
    cout << "SignOffUserDoesNotHaveAnActiveSession SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_off_result.first);
  }

  TEST_FIXTURE(UserFixture, SignOffUserDoesNotExist)
  {
    string fake_userid {"John"};

    //sign off non existing user
    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + fake_userid);
    cout << "SignOffUserDoesNotExist SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::NotFound, sign_off_result.first);
  }

  TEST_FIXTURE(UserFixture, MalformedRequest)
  {
    //malformed requests 
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + read_entity_admin + "/" + UserFixture::user1_id, 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string(UserFixture::user1_password))}));
    cout << "MalformedRequest SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, sign_on_result.first);

    pair<status_code, value> sign_off_result = do_request(methods::GET, string(user_url) + sign_off + "/" + UserFixture::user1_id);
    cout << "MalformedRequest SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, sign_off_result.first);

    sign_off_result = do_request(methods::PUT, string(user_url) + sign_off + "/" + UserFixture::user1_id);
    cout << "MalformedRequest SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, sign_off_result.first);
  }

    TEST_FIXTURE(UserFixture, DisallowedRequest)
  {
    //DisallowedRequest
    pair<status_code, value> sign_on_result = do_request(methods::DEL, string(user_url) + read_entity_admin + "/" + UserFixture::user1_id, 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string(UserFixture::user1_password))}));
    cout << "DisallowedRequest SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::MethodNotAllowed, sign_on_result.first);
  }

  /////////////////////////////////////////////
  //                                         //
  //             Andrew's tests              //
  //                                         //
  /////////////////////////////////////////////

  TEST_FIXTURE(UserFixture, AddFriendUser1)
  {
    pair<status_code,value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string(UserFixture::user1_password))}));
    cout << "SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    auto add_friend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      add_friend + "/" +
                                      UserFixture::user1_id + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      UserFixture::user2_DataRow);

    cout << "Add Friend response: " << add_friend_res.first << endl;
    CHECK_EQUAL(status_codes::OK, add_friend_res.first);

    add_friend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      add_friend + "/" +
                                      "dankmemes" + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      UserFixture::user2_DataRow);
    cout << "Add Friend while not active session response: " << add_friend_res.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, add_friend_res.first);

    add_friend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      add_friend + "/" +
                                      UserFixture::user1_id + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      UserFixture::user1_DataRow);
    cout << "Add Self: (SHOULD THIS BE: " << add_friend_res.first << ")" << endl;
    CHECK_EQUAL(status_codes::OK, add_friend_res.first);

    add_friend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      add_friend + "/" +
                                      UserFixture::user1_id + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      "EVENMORE,DANKMEMES");
    cout << "Add random string as friend: " << add_friend_res.first << endl;
    CHECK_EQUAL(status_codes::OK, add_friend_res.first);

    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + UserFixture::user1_id);
    cout << "SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);
  }

  TEST_FIXTURE(UserFixture, UnfriendUser1)
  {
    pair<status_code,value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string(UserFixture::user1_password))}));
    cout << "SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    auto unfriend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      unfriend + "/" +
                                      UserFixture::user1_id + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      UserFixture::user2_DataRow);

    cout << "UnFriend response (added from previous TEST_FIXTURE: " << unfriend_res.first << endl;
    CHECK_EQUAL(status_codes::OK, unfriend_res.first);

    unfriend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      unfriend + "/" +
                                      UserFixture::user1_id + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      UserFixture::user3_DataRow);

    cout << "UnFriend response (not added): " << unfriend_res.first << endl;
    CHECK_EQUAL(status_codes::OK, unfriend_res.first);

    unfriend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      unfriend + "/" +
                                      "dankmemes" + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      UserFixture::user2_DataRow);
    cout << "UnFriend while not active session response: " << unfriend_res.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, unfriend_res.first);

    unfriend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      unfriend + "/" +
                                      UserFixture::user1_id + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      UserFixture::user1_DataRow);
    cout << "UnFriend Self (Probably 200: " << unfriend_res.first << ")" << endl;
    CHECK_EQUAL(status_codes::OK, unfriend_res.first);

    unfriend_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      unfriend + "/" +
                                      UserFixture::user1_id + "/" +
                                      UserFixture::user1_DataPartition + "/" +
                                      "opap,bankgp");
    cout << "Unfriend random string as friend: " << unfriend_res.first << endl;
    CHECK_EQUAL(status_codes::OK, unfriend_res.first);

    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + UserFixture::user1_id);
    cout << "SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);
  }

  TEST_FIXTURE(UserFixture, UpdateStatusUser1)
  {
    pair<status_code,value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + UserFixture::user1_id, 
        value::object (vector<pair<string,value>>{make_pair("Password", value::string(UserFixture::user1_password))}));
    cout << "SignOn response " << sign_on_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);


    auto update_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      update_status + "/" +
                                      UserFixture::user1_id + "/" +
                                      "CRAZYMEMESAREUS");

    cout << "Update status on logged in user (no spaces): " << update_res.first << endl;
    CHECK_EQUAL(status_codes::OK, update_res.first);

    update_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      update_status + "/" +
                                      UserFixture::user1_id + "/" +
                                      "CR,AZY_ME.MES_AR,E_US");
    cout << "Update status on logged in user (underscore and punctuations): " << update_res.first << endl;
    CHECK_EQUAL(status_codes::OK, update_res.first);

    update_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      update_status + "/" +
                                      UserFixture::user1_id + "/" +
                                      "_");
    cout << "Update status on logged in user with one char: " << update_res.first << endl;
    CHECK_EQUAL(status_codes::OK, update_res.first);

    update_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      update_status + "/" +
                                      "dankmemes" + "/" +
                                      "CRAZYMEMESAREUS");
    cout << "Update status on not logged in user: " << update_res.first << endl;

    CHECK_EQUAL(status_codes::Forbidden, update_res.first);

    update_res = do_request(methods::PUT,
                                      string(user_url) + 
                                      update_status + "/" +
                                      "dankmemes" + "/" +
                                      "CRAZY_MEMES_ARE_US");
    cout << "Update status on not logged in user + underscore: " << update_res.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, update_res.first);

    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + UserFixture::user1_id);
    cout << "SignOff response " << sign_off_result.first << endl;
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);
  }
}

//GetFriendsList tests
SUITE(GetFriendsList)
{

  class GetFriendsListFixture
  {
  public:
     
      static constexpr const char* user1_id {"Lawrence"};
      static constexpr const char* user1_password {"Yu"};
      static constexpr const char* user1_DataPartition {"Canada"};
      static constexpr const char* user1_DataRow {"Yu,Lawrence"};

      static constexpr const char* user2_id {"Josh"};
      static constexpr const char* user2_password {"Fernandez"};
      static constexpr const char* user2_DataPartition {"ThePhilippines"};
      static constexpr const char* user2_DataRow {"Fernandez,Josh"};

      static constexpr const char* user3_id {"Andrew"};
      static constexpr const char* user3_password {"Song"};
      static constexpr const char* user3_DataPartition {"Korea"};
      static constexpr const char* user3_DataRow {"Song,Andrew"};

      static constexpr const char* user4_id {"Angel"};
      static constexpr const char* user4_password {"Singh"};
      static constexpr const char* user4_DataPartition {"Korea"};
      static constexpr const char* user4_DataRow {"Singh,Angel"};
  public:
    GetFriendsListFixture()
    {  
      //Initialize AuthTable users
      pair<status_code,value> put_result {

        do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string(user1_id),
                    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user1_password)),
                                                              make_pair("DataPartition", value::string(user1_DataPartition)),
                                                              make_pair("DataRow", value::string(user1_DataRow))}))

      };
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }

      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string(user2_id),
                    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user2_password)),
                                                              make_pair("DataPartition", value::string(user2_DataPartition)),
                                                              make_pair("DataRow", value::string(user2_DataRow))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }


      //Initialize DataTable users
      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + user1_DataPartition + "/" + user1_DataRow,
                    value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                              make_pair("Status", value::string("")),
                                                              make_pair("Updates", value::string(""))}));

      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }

      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + user2_DataPartition + "/" + user2_DataRow,
                    value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                              make_pair("Status", value::string("")),
                                                              make_pair("Updates", value::string(""))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }
    }

    ~GetFriendsListFixture()
    {
      //Delete AuthTable users
      int del_ent_result {delete_entity (basic_url, auth_table_name, auth_table_partition, user1_id)};
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }

      del_ent_result = delete_entity (basic_url, auth_table_name, auth_table_partition, user2_id);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }


      //Delete DataTable users
      del_ent_result = delete_entity (basic_url, data_table_name, user1_DataPartition, user1_DataRow);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }

      del_ent_result = delete_entity (basic_url, data_table_name, user2_DataPartition, user2_DataRow);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
    }

  };

  TEST_FIXTURE(GetFriendsListFixture, SuccessfullyReturnsEmptyFriendsList)
  {
    //SignOn
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user1_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user1_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user2_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user2_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    //Return empty friends list
    pair<status_code,value> read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsEmptyFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string(""))}), read_friend_list_result.second);

    //Return empty friends list
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user2_id);
    cout << "SuccessfullyReturnsEmptyFriendsList User2 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string(""))}), read_friend_list_result.second);

    //SignOff
    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user1_id);
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);
    sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user2_id);
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);
  }

  TEST_FIXTURE(GetFriendsListFixture, UseridDoesNotHaveAnActiveSession)
  {
    //Non existing user
    pair<status_code,value> read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + "rgf");
    cout << "UseridDoesNotHaveAnActiveSession User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, read_friend_list_result.first);

    //inactive user
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "UseridDoesNotHaveAnActiveSession User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, read_friend_list_result.first);

    //SignOn and Off
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user1_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user1_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);
    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user1_id);
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);

    //inactive user
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "UseridDoesNotHaveAnActiveSession User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, read_friend_list_result.first);
  }

  TEST_FIXTURE(GetFriendsListFixture, SuccessfullyReturnsProperlyFormattedFriendsList)
  {
    //SignOn user1
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user1_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user1_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    //Addfriend user1
    pair<status_code, value> add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user1_id + "/" + "fake_country" + "/" + "friend,fake");
    CHECK_EQUAL(status_codes::OK, add_friend_result.first);

    //Return friends list user1
    pair<status_code,value> read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("fake_country;friend,fake"))}), read_friend_list_result.second);

    //SignOff user1
    pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user1_id);
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);

    //Addfriend user1
    add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user1_id + "/" + "fake_csdountry" + "/" + "friend,sdsdfake");
    CHECK_EQUAL(status_codes::Forbidden, add_friend_result.first);

    //Return friends list user1
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::Forbidden, read_friend_list_result.first);

    //SignOn user1
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user1_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user1_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    //Addfriend user1
    add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user1_id + "/" + user2_DataPartition + "/" + user2_DataRow);
    CHECK_EQUAL(status_codes::OK, add_friend_result.first);

    //Return friends list user1
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("fake_country;friend,fake|ThePhilippines;Fernandez,Josh"))}), read_friend_list_result.second);

    //SignOn user2
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user2_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user2_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);

    //Addfriend user1
    add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user1_id + "/" + user3_DataPartition + "/" + user3_DataRow);
    CHECK_EQUAL(status_codes::OK, add_friend_result.first);

    //Return friends list user1
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("fake_country;friend,fake|ThePhilippines;Fernandez,Josh|Korea;Song,Andrew"))}), read_friend_list_result.second);

    //Addfriend user2
    add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user2_id + "/" + user3_DataPartition + "/" + user3_DataRow);
    CHECK_EQUAL(status_codes::OK, add_friend_result.first);

    //Return friends list user2
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user2_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User2 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("Korea;Song,Andrew"))}), read_friend_list_result.second);

    //Addfriend user1
    add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user1_id + "/" + user2_DataPartition + "/" + user2_DataRow);
    CHECK_EQUAL(status_codes::OK, add_friend_result.first);

    //Return friends list user1
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("fake_country;friend,fake|ThePhilippines;Fernandez,Josh|Korea;Song,Andrew"))}), read_friend_list_result.second);

    //unfriend user1
    pair<status_code, value> un_friend_result = do_request(methods::PUT, string(user_url) + unfriend + "/" + user1_id + "/" + user2_DataPartition + "/" + user2_DataRow);
    CHECK_EQUAL(status_codes::OK, un_friend_result.first);

    //Return friends list user1
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("fake_country;friend,fake|Korea;Song,Andrew"))}), read_friend_list_result.second);

    //unfriend user1
    un_friend_result = do_request(methods::PUT, string(user_url) + unfriend + "/" + user1_id + "/" + "fake_country" + "/" + "friend,fake");
    CHECK_EQUAL(status_codes::OK, un_friend_result.first);

    //Return friends list user1
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("Korea;Song,Andrew"))}), read_friend_list_result.second);

    //unfriend user1
    un_friend_result = do_request(methods::PUT, string(user_url) + unfriend + "/" + user1_id + "/" + user3_DataPartition + "/" + user3_DataRow);
    CHECK_EQUAL(status_codes::OK, un_friend_result.first);

    //Return friends list user1
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User1 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string(""))}), read_friend_list_result.second);

    //SignOff user1
    sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user1_id);
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);

    //unfriend user1
    un_friend_result = do_request(methods::PUT, string(user_url) + unfriend + "/" + user1_id + "/" + "fake_country" + "/" + "friend,fake");
    CHECK_EQUAL(status_codes::Forbidden, un_friend_result.first);

    //unfriend user2
    un_friend_result = do_request(methods::PUT, string(user_url) + unfriend + "/" + user2_id + "/" + user3_DataPartition + "/" + user3_DataRow);
    CHECK_EQUAL(status_codes::OK, un_friend_result.first);

    //Return friends list user2
    read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user2_id);
    cout << "SuccessfullyReturnsProperlyFormattedFriendsList User2 ReadFriendList response " << read_friend_list_result.first << endl;
    CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
    CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string(""))}), read_friend_list_result.second);

    //SignOff user2
    sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user2_id);
    CHECK_EQUAL(status_codes::OK, sign_off_result.first);
  }
}


//PushStatus tests
SUITE(PushStatus)
{

  class PushStatusFixture
  {
  public:
     
      static constexpr const char* user1_id {"Lawrence"};
      static constexpr const char* user1_password {"Yu"};
      static constexpr const char* user1_DataPartition {"Canada"};
      static constexpr const char* user1_DataRow {"Yu,Lawrence"};

      static constexpr const char* user2_id {"Josh"};
      static constexpr const char* user2_password {"Fernandez"};
      static constexpr const char* user2_DataPartition {"ThePhilippines"};
      static constexpr const char* user2_DataRow {"Fernandez,Josh"};

      static constexpr const char* user3_id {"Andrew"};
      static constexpr const char* user3_password {"Song"};
      static constexpr const char* user3_DataPartition {"Korea"};
      static constexpr const char* user3_DataRow {"Song,Andrew"};

      static constexpr const char* user4_id {"Angel"};
      static constexpr const char* user4_password {"Singh"};
      static constexpr const char* user4_DataPartition {"Korea"};
      static constexpr const char* user4_DataRow {"Singh,Angel"};
  public:
    PushStatusFixture()
    {  
      //Initialize AuthTable users
      pair<status_code,value> put_result {

        do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string(user1_id),
                    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user1_password)),
                                                              make_pair("DataPartition", value::string(user1_DataPartition)),
                                                              make_pair("DataRow", value::string(user1_DataRow))}))

      };
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }
      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string(user2_id),
                    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user2_password)),
                                                              make_pair("DataPartition", value::string(user2_DataPartition)),
                                                              make_pair("DataRow", value::string(user2_DataRow))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }
      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string(user3_id),
                    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user3_password)),
                                                              make_pair("DataPartition", value::string(user3_DataPartition)),
                                                              make_pair("DataRow", value::string(user3_DataRow))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }
     put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + auth_table_name + "/" + auth_table_partition + "/" + string(user4_id),
                    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user4_password)),
                                                              make_pair("DataPartition", value::string(user4_DataPartition)),
                                                              make_pair("DataRow", value::string(user4_DataRow))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }


      //Initialize DataTable users
      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + user1_DataPartition + "/" + user1_DataRow,
                    value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                              make_pair("Status", value::string("")),
                                                              make_pair("Updates", value::string(""))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }
      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + user2_DataPartition + "/" + user2_DataRow,
                    value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                              make_pair("Status", value::string("")),
                                                              make_pair("Updates", value::string(""))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }
      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + user3_DataPartition + "/" + user3_DataRow,
                    value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                              make_pair("Status", value::string("")),
                                                              make_pair("Updates", value::string(""))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }
      put_result = do_request (methods::PUT,
                    string(basic_url) + update_entity_admin + "/" + data_table_name + "/" + user4_DataPartition + "/" + user4_DataRow,
                    value::object (vector<pair<string,value>>{make_pair("Friends", value::string("")),
                                                              make_pair("Status", value::string("")),
                                                              make_pair("Updates", value::string(""))}));
      if (put_result.first != status_codes::OK) {
        throw std::exception();
      }

    //Sign on all users
    pair<status_code, value> sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user1_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user1_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user2_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user2_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user3_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user3_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);
    sign_on_result = do_request(methods::POST, string(user_url) + sign_on + "/" + user4_id, 
    value::object (vector<pair<string,value>>{make_pair("Password", value::string(user4_password))}));
    CHECK_EQUAL(status_codes::OK, sign_on_result.first);


      //initialize friend lists
      //Addfriend user1
      pair<status_code, value> add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user1_id + "/" + user1_DataPartition + "/" + user1_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user1_id + "/" + user2_DataPartition + "/" + user2_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user1_id + "/" + user3_DataPartition + "/" + user3_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      //Return friends list user1
      pair<status_code, value> read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
      CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
      CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("Canada;Yu,Lawrence|ThePhilippines;Fernandez,Josh|Korea;Song,Andrew"))}), read_friend_list_result.second);


      //Addfriend user2
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user2_id + "/" + user1_DataPartition + "/" + user1_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user2_id + "/" + "fake_country" + "/" + "friend,fake");
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user2_id + "/" + user4_DataPartition + "/" + user4_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user2_id + "/" + "USA" + "/" + "Joe");
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      //Return friends list user2
      read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user2_id);
      CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
      CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("Canada;Yu,Lawrence|fake_country;friend,fake|Korea;Singh,Angel|USA;Joe"))}), read_friend_list_result.second);


      //Addfriend user4
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user4_id + "/" + user4_DataPartition + "/" + user4_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user4_id + "/" + user4_DataPartition + "/" + user4_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user4_id + "/" + user4_DataPartition + "/" + user4_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user4_id + "/" + user4_DataPartition + "/" + user4_DataRow);
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      //Return friends list user3
      read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user4_id);
      CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
      CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("Korea;Singh,Angel"))}), read_friend_list_result.second);


      //Addfriend user3
      add_friend_result = do_request(methods::PUT, string(user_url) + add_friend + "/" + user3_id + "/" + "fake_country" + "/" + "friend,fake");
      CHECK_EQUAL(status_codes::OK, add_friend_result.first);
      //Return friends list user1
      read_friend_list_result = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user3_id);
      CHECK_EQUAL(status_codes::OK, read_friend_list_result.first);
      CHECK_EQUAL(value::object(vector<pair<string,value>>{make_pair("Friends", value::string("fake_country;friend,fake"))}), read_friend_list_result.second);
    }

    ~PushStatusFixture()
    {
      //SignOff all users
      pair<status_code, value> sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user1_id);
      CHECK_EQUAL(status_codes::OK, sign_off_result.first);
      sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user2_id);
      CHECK_EQUAL(status_codes::OK, sign_off_result.first);
      sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user3_id);
      CHECK_EQUAL(status_codes::OK, sign_off_result.first);
      sign_off_result = do_request(methods::POST, string(user_url) + sign_off + "/" + user4_id);
      CHECK_EQUAL(status_codes::OK, sign_off_result.first);

      //Delete AuthTable users
      int del_ent_result {delete_entity (basic_url, auth_table_name, auth_table_partition, user1_id)};
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
      del_ent_result = delete_entity (basic_url, auth_table_name, auth_table_partition, user2_id);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
      del_ent_result = delete_entity (basic_url, auth_table_name, auth_table_partition, user3_id);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
      del_ent_result = delete_entity (basic_url, auth_table_name, auth_table_partition, user4_id);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }


      //Delete DataTable users
      del_ent_result = delete_entity (basic_url, data_table_name, user1_DataPartition, user1_DataRow);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
      del_ent_result = delete_entity (basic_url, data_table_name, user2_DataPartition, user2_DataRow);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
      del_ent_result = delete_entity (basic_url, data_table_name, user3_DataPartition, user3_DataRow);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
      del_ent_result = delete_entity (basic_url, data_table_name, user4_DataPartition, user4_DataRow);
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }
    }

  };

  TEST_FIXTURE(PushStatusFixture, SuccessfullyPushStatusUpdateToAllFriends)
  {
    //Normal PushStatus user1, 1 updates
    pair <status_code, value> friends_list { do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id) };
    pair<status_code, value> push_status_result { do_request(methods::POST, push_url + push_status + "/" + user1_DataPartition + "/" + user1_DataRow + "/" + "HAPPY_FACE", friends_list.second)};
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 PushStatus response " << push_status_result.first << endl << endl;
    CHECK_EQUAL(status_codes::OK, push_status_result.first);

    pair<status_code, value> get_result { do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user1_DataPartition + "/" + user1_DataRow) };
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\n", get_result.second["Updates"].as_string());
    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user2_DataPartition + "/" + user2_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User2 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\n", get_result.second["Updates"].as_string());
    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user3_DataPartition + "/" + user3_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User3 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\n", get_result.second["Updates"].as_string());

    //Normal PushStatus user1, 2 updates
    friends_list = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    push_status_result = do_request(methods::POST, push_url + push_status + "/" + user1_DataPartition + "/" + user1_DataRow + "/" + "sad_face", friends_list.second);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 PushStatus response " << push_status_result.first << endl << endl;
    CHECK_EQUAL(status_codes::OK, push_status_result.first);

    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user1_DataPartition + "/" + user1_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\nsad_face\n", get_result.second["Updates"].as_string());
    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user2_DataPartition + "/" + user2_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User2 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\nsad_face\n", get_result.second["Updates"].as_string());
    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user3_DataPartition + "/" + user3_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User3 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\nsad_face\n", get_result.second["Updates"].as_string());

    //Normal PushStatus user1, 3 updates
    friends_list = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    push_status_result = do_request(methods::POST, push_url + push_status + "/" + user1_DataPartition + "/" + user1_DataRow + "/" + "Ayyyyyy", friends_list.second);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 PushStatus response " << push_status_result.first << endl << endl;
    CHECK_EQUAL(status_codes::OK, push_status_result.first);

    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user1_DataPartition + "/" + user1_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\nsad_face\nAyyyyyy\n", get_result.second["Updates"].as_string());
    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user2_DataPartition + "/" + user2_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User2 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\nsad_face\nAyyyyyy\n", get_result.second["Updates"].as_string());
    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user3_DataPartition + "/" + user3_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User3 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\nsad_face\nAyyyyyy\n", get_result.second["Updates"].as_string());

    //Normal PushStatus user2, real + not real friends
    friends_list = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user2_id);
    push_status_result = do_request(methods::POST, push_url + push_status + "/" + user2_DataPartition + "/" + user2_DataRow + "/" + "wow", friends_list.second);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User2 PushStatus response " << push_status_result.first << endl << endl;
    CHECK_EQUAL(status_codes::OK, push_status_result.first);

    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user1_DataPartition + "/" + user1_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("HAPPY_FACE\nsad_face\nAyyyyyy\nwow\n", get_result.second["Updates"].as_string());
    get_result = do_request(methods::GET, basic_url + read_entity_admin + "/" + data_table_name + "/" + user4_DataPartition + "/" + user4_DataRow);
    CHECK_EQUAL(status_codes::OK, get_result.first);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User4 Updates: " << get_result.second["Updates"].as_string() << endl;
    CHECK_EQUAL("wow\n", get_result.second["Updates"].as_string());

    //less than 4 parameters
    friends_list = do_request(methods::GET, string(user_url) + read_friend_list + "/" + user1_id);
    push_status_result = do_request(methods::POST, push_url + push_status + "/" + user1_DataRow + "/" + "sad_face", friends_list.second);
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 PushStatus response " << push_status_result.first << endl << endl;
    CHECK_EQUAL(status_codes::BadRequest, push_status_result.first);

    //malformed request
    push_status_result = do_request(methods::POST, string(push_url) + "aeworigshoiwasghoiwejgoiwaejg");
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 PushStatus response " << push_status_result.first << endl << endl;
    CHECK_EQUAL(status_codes::BadRequest, push_status_result.first);

    //malformed request
    push_status_result = do_request(methods::POST, string(push_url) + "/tg/5t/6/r4/g/g/y/7/6/5/g/f/4/34");
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 PushStatus response " << push_status_result.first << endl << endl;
    CHECK_EQUAL(status_codes::BadRequest, push_status_result.first);

    //no properties
    push_status_result = do_request(methods::POST, push_url + push_status + "/" + user1_DataPartition + "/" + user1_DataRow + "/" + "sad_face");
    cout << "SuccessfullyPushStatusUpdateToAllFriends User1 PushStatus response " << push_status_result.first << endl << endl;
    CHECK_EQUAL(status_codes::OK, push_status_result.first);

  }
}