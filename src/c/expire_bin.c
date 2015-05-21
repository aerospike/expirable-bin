/*
 * Copyright 2014 Aerospike, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 //==========================================================
// Includes
//

#include <errno.h>

#include <aerospike/aerospike_key.h>
#include <aerospike/aerospike_scan.h>
#include <aerospike/as_arraylist.h>
#include <aerospike/aerospike_udf.h>
#include <aerospike/as_hashmap.h>
#include <aerospike/as_stringmap.h>
#include <aerospike/aerospike_scan.h>

//==========================================================
// Constants
//

#define UDF_MODULE "expire_bin"
#define UDF_USER_PATH "../../"
#define LOG(_fmt, _args...) { printf(_fmt "\n", ## _args); fflush(stdout); }

const char UDF_FILE_PATH[] = UDF_USER_PATH UDF_MODULE ".lua";

// Namespace, Set, and Key	
const char DEFAULT_NAMESPACE[] = "test";
const char DEFAULT_SET[] = "expireBin";
const char DEFAULT_KEY_STR[] = "testKey";
// Based on current server limit
char eb_namespace[32]; 
char eb_set[64];
char eb_key_str[1024];

//==========================================================
// Forward Declarations
//

as_val* as_expbin_get(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, as_list* arglist, as_val* result);
void as_expbin_put(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, char* bin, as_val* val, uint64_t bin_ttl, as_val* result);
void as_expbin_puts(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, as_list* arglist, as_val* result);
void as_expbin_touch(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, as_list* arglist, as_val* result);
as_val* as_expbin_ttl(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, char* bin_name, as_val* result);
void as_expbin_clean(aerospike* as, as_error* err, const as_policy_scan* policy, as_scan* scan, as_list* binlist);
as_hashmap create_bin_map(char* bin_name, char* val, int64_t bin_ttl);
bool register_udf(aerospike* p_as, const char* udf_file_path);
void cleanup(aerospike* as, as_error* err, as_policy_remove* policy, as_key* key);

//==========================================================
// Expire Bin C Example.
//  

int
main(int argc, char* argv[]) 
{
	LOG("This is a demo of the expirable bin module for C");

	strcpy(eb_namespace, DEFAULT_NAMESPACE);
	strcpy(eb_set, DEFAULT_SET);
	strcpy(eb_key_str, DEFAULT_KEY_STR);

	aerospike as;
	as_config config;
	as_error err;

	as_config_init(&config);
	as_config_add_host(&config, "127.0.0.1", 3000);
	aerospike_init(&as, &config);

	LOG("Connecting to Aerospike server...");
	
	if (aerospike_connect(&as, &err) != AEROSPIKE_OK) {
		LOG("error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
		exit(1);
	}

	LOG("Connected!");

	as_key testKey;

	// Start clean.
	aerospike_key_remove(&as, &err, NULL, &testKey);
	
	if (as_key_init_str(&testKey, eb_namespace, eb_set, eb_key_str) == NULL) {
		LOG("Key was not initiated");
		exit(1);
	}

	LOG("Registering UDF...");

	if (! register_udf(&as, UDF_FILE_PATH)) {
		LOG("Error registering UDF!")
		cleanup(&as, &err, NULL, &testKey);
		exit(-1);
	}

	LOG("UDF registered!");

	as_val* result = NULL;
	as_string val;
	as_arraylist arglist;

	LOG("Creating expire bins...");
	
	as_string_init(&val, "Hello World.", false);
	as_expbin_put(&as, &err, NULL, &testKey, "TestBin1", (as_val*)&val, -1, result);

	as_string_init(&val, "I don't expire.", false);
	as_expbin_put(&as, &err, NULL, &testKey, "TestBin2", (as_val*)&val, -1, result);

	as_string_init(&val, "I will expire soon.", false);
	as_expbin_put(&as, &err, NULL, &testKey, "TestBin3", (as_val*)&val, 5, result);

	as_hashmap map1, map2; 
	as_arraylist_inita(&arglist, 2);

	map1 = create_bin_map("TestBin4", "Good Morning.", 100); 
	as_val_reserve((as_map *)&map1);
	as_arraylist_append(&arglist, (as_val *)((as_map *)&map1));

	map2 = create_bin_map("TestBin5", "Good Night.", 0); 
	as_val_reserve((as_map *)&map2);
	as_arraylist_append(&arglist, (as_val *)((as_map *)&map2));

	as_expbin_puts(&as, &err, NULL, &testKey, (as_list*)&arglist, result);

	LOG("Getting expire bins...");

	as_arraylist_inita(&arglist, 5);
	as_arraylist_append_str(&arglist, "TestBin1");
	as_arraylist_append_str(&arglist, "TestBin2");
	as_arraylist_append_str(&arglist, "TestBin3");
	as_arraylist_append_str(&arglist, "TestBin4");
	as_arraylist_append_str(&arglist, "TestBin5");

	result = as_expbin_get(&as, &err, NULL, &testKey, (as_list*)&arglist, result);
	LOG("TestBins: %s", as_val_tostring(result));

	LOG("Getting bin TTLs...");
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin1", result); 
	LOG("TestBin 1 TTL: %s", as_val_tostring(result));
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin2", result); 
	LOG("TestBin 2 TTL: %s", as_val_tostring(result));
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin3", result); 
	LOG("TestBin 3 TTL: %s", as_val_tostring(result));
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin4", result); 
	LOG("TestBin 4 TTL: %s", as_val_tostring(result));
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin5", result); 
	LOG("TestBin 5 TTL: %s", as_val_tostring(result));

	LOG("Waiting for TestBin 3 to expire...");

	sleep(10);

	LOG("Getting expire bins again...");

	as_arraylist_inita(&arglist, 5);
	as_arraylist_append_str(&arglist, "TestBin1");
	as_arraylist_append_str(&arglist, "TestBin2");
	as_arraylist_append_str(&arglist, "TestBin3");
	as_arraylist_append_str(&arglist, "TestBin4");
	as_arraylist_append_str(&arglist, "TestBin5");

	result = as_expbin_get(&as, &err, NULL, &testKey, (as_list*)&arglist, result);
	LOG("TestBins: %s", as_val_tostring(result));

	LOG("Changing expiration times...");

	as_arraylist_inita(&arglist, 2);

	map1 = create_bin_map("TestBin1", "Hello World.", 10); 
	as_val_reserve((as_map *)&map1);
	as_arraylist_append(&arglist, (as_val *)((as_map *)&map1));

	map2 = create_bin_map("TestBin4", "Good Morning.", 5); 
	as_val_reserve((as_map *)&map2);
	as_arraylist_append(&arglist, (as_val *)((as_map *)&map2));

	as_expbin_touch(&as, &err, NULL, &testKey, (as_list*)&arglist, result);

	LOG("Getting bin TTLs...");
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin1", result); 
	LOG("TestBin 1 TTL: %s", as_val_tostring(result));
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin2", result); 
	LOG("TestBin 2 TTL: %s", as_val_tostring(result));
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin3", result); 
	LOG("TestBin 3 TTL: %s", as_val_tostring(result));
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin4", result); 
	LOG("TestBin 4 TTL: %s", as_val_tostring(result));
	result = as_expbin_ttl(&as, &err, NULL, &testKey, "TestBin5", result); 
	LOG("TestBin 5 TTL: %s", as_val_tostring(result));

	LOG("Cleaning bins...");

	as_scan scan;

	as_arraylist_inita(&arglist, 5);
	as_arraylist_append_str(&arglist, "TestBin1");
	as_arraylist_append_str(&arglist, "TestBin2");
	as_arraylist_append_str(&arglist, "TestBin3");
	as_arraylist_append_str(&arglist, "TestBin4");
	as_arraylist_append_str(&arglist, "TestBin5");

	LOG("Scan in progress...");
	as_expbin_clean(&as, &err, NULL, &scan, (as_list*) &arglist);
	LOG("Scan completed!");

	LOG("Checking expire bins again...");

	as_arraylist_inita(&arglist, 5);
	as_arraylist_append_str(&arglist, "TestBin1");
	as_arraylist_append_str(&arglist, "TestBin2");
	as_arraylist_append_str(&arglist, "TestBin3");
	as_arraylist_append_str(&arglist, "TestBin4");
	as_arraylist_append_str(&arglist, "TestBin5");

	result = as_expbin_get(&as, &err, NULL, &testKey, (as_list*) &arglist, result);
	LOG("TestBins: %s", as_val_tostring(result));

	aerospike_close(&as, &err);
	aerospike_destroy(&as);

	return 0;
}

/*
 * Attempt to retrieve values from list of bins. The bins
 * can be expire bins or normal bins.
 *
 * \param as      - The aerospike instance to use for this operation.
 * \param err     - The as_error to be populated if an error occurs.
 * \param policy  - The policy to use for this operation. If NULL, then the default policy will be used.
 * \param key     - The key of the record.
 * \param arglist - The list of bin names to retrieve values from.
 * \param result  - A list of bin values respective to the list of bin names passed in. 
 *                  If a bin is expired or empty, the corresponding index in the list will be NULL.
 * \return        - result if successful, an error otherwise.
 */
as_val* 
as_expbin_get(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, as_list* arglist, as_val* result)
{
	as_status rc = aerospike_key_apply(as, err, policy, key, UDF_MODULE, "get", arglist, &result);
	
	if (rc != AEROSPIKE_OK) {
		LOG("as_expbin_get() returned %d - %s", err->code, err->message);
		exit(1);
	}
	return result;
}

/*
 * Create or update expire bins. If bin_ttl is not NULL, all newly created bins
 * will be expire bins, otherwise, only normal bins will be created and existing 
 * expire bins will be updated. Note: existing expire bins will not be converted
 * into normal bins if bin_ttl is NULL.
 *
 * \param as      - The aerospike instance to use for this operation.
 * \param err     - The as_error to be populated if an error occurs.
 * \param policy  - The policy to use for this operation. If NULL, then the default policy will be used.
 * \param key     - The key of the record.
 * \param bin     - Bin name.
 * \param val     - Bin value.
 * \param bin_ttl - Expiration time in seconds or -1 for no expiration.
 * \param result  - 0 if successfully written, 1 otherwise.
 * \return        - void if successful, an error otherwise.
 */
void
as_expbin_put(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, char* bin, as_val* val, uint64_t bin_ttl, as_val* result)
{
	as_arraylist arglist;
	as_arraylist_inita(&arglist, 4);
	as_arraylist_append_str(&arglist, bin);
	as_arraylist_append(&arglist, val);
	as_arraylist_append_int64(&arglist, bin_ttl);

	as_status rc = aerospike_key_apply(as, err, policy, key, UDF_MODULE, "put", (as_list*)&arglist, &result);
	
	if (rc != AEROSPIKE_OK) {
		LOG("as_expbin_put() returned %d - %s", err->code, err->message);
		exit(1);
	}
}

/* Batch create or update expire bins for a given key. Use the as_map:
 * {'bin' : bin_name, 'val' : bin_value, 'bin_ttl' : ttl} to store each put operation.
 * Omit the bin_ttl to turn bin creation off.
 *
 * \param as      - The aerospike instance to use for this operation.
 * \param err     - The as_error to be populated if an error occurs.
 * \param policy  - The policy to use for this operation. If NULL, then the default policy will be used.
 * \param key     - The key of the record.
 * \param arglist - The list of as_maps in the following form: {'bin' : bin_name, 'val' : bin_value, 'bin_ttl' : ttl}.
 * \param result  - 0 if all ops succeed, 1 otherwise.
 * \return        - void if successful, an error otherwise.
 */
void 
as_expbin_puts(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, as_list* arglist, as_val* result) 
{
	as_status rc = aerospike_key_apply(as, err, policy, key, UDF_MODULE, "puts", arglist, &result);
	
	if (rc != AEROSPIKE_OK) {
		LOG("as_expbin_puts() returned %d - %s", err->code, err->message);
		exit(1);
	}
}

/*
 * Batch update the bin TTLs. Us this method to change or reset the bin TTL of
 * multiple bins in a record. 
 *
 * \param as      - The aerospike instance to use for this operation.
 * \param err     - The as_error to be populated if an error occurs.
 * \param policy  - The policy to use for this operation. If NULL, then the default policy will be used.
 * \param key     - The key of the record.
 * \param arglist - The list of bin names.
 * \param result  - 0 if all ops succeed, 1 otherwise.
 * \return        - void if successful, an error otherwise.
 */
void 
as_expbin_touch(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, as_list* arglist, as_val* result) 
{
	as_status rc = aerospike_key_apply(as, err, policy, key, UDF_MODULE, "touch", arglist, &result);
	
	if (rc != AEROSPIKE_OK) {
		LOG("as_expbin_touch() returned %d - %s", err->code, err->message);	
		exit(1);
	}
}

/*
 * Get the time bin will expire in seconds.
 *
 * \param as      - The aerospike instance to use for this operation.
 * \param err     - The as_error to be populated if an error occurs.
 * \param policy  - The policy to use for this operation. If NULL, then the default policy will be used.
 * \param key     - The key of the record.
 * \param arglist - The bin name to check.
 * \param result  - Bin time to expire in seconds.
 * \return        - result if successful, an error otherwise.
 */
as_val*
as_expbin_ttl(aerospike* as, as_error* err, as_policy_apply* policy, as_key* key, char* bin_name, as_val* result) 
{
	as_arraylist arglist;
	as_arraylist_inita(&arglist, 1);
	as_arraylist_append_str(&arglist, bin_name);

	as_status rc = aerospike_key_apply(as, err, policy, key, UDF_MODULE, "ttl", (as_list*) &arglist, &result);
	
	if (rc != AEROSPIKE_OK) {
		LOG("as_expbin_ttl() returned %d - %s", err->code, err->message);
		exit(1);	
	}
	return result;
}

/* 
 * Perform a background scan and remove all expired bins.
 * 
 * \param as      - The aerospike instance to use for this operation.
 * \param err     - The as_error to be populated if an error occurs.
 * \param policy  - The policy to use for this operation. If NULL, then the default policy will be used.
 * \param scan    - as_scan to execute scan on.
 * \param binlist - List of bins to clean.
 * \return        - void if successful, an error otherwise.
 */
void
as_expbin_clean(aerospike* as, as_error* err, const as_policy_scan* policy, as_scan* scan, as_list* binlist)
{
	uint64_t scan_id = 0;
	as_scan_init(scan, eb_namespace, eb_set);

	if (as_scan_apply_each(scan, UDF_MODULE, "clean", binlist) != true) {
		LOG("UDF apply failed");
		exit(1);
	}

	as_status rc = aerospike_scan_background(as, err, policy, scan, &scan_id);
	aerospike_scan_wait(as, err, NULL, scan_id, 0);

	if (rc != AEROSPIKE_OK) {
		LOG("as_expbin_clean() returned %d - %s", err->code, err->message);
		exit(1);
	} 
}

/*
 * Generate maps for use with batch put and touch operations.
 *
 * \param bin_name - name of bin to perform op on.
 * \param val      - value of bin.
 * \param bin_ttl  - bin_ttl for bin (-1 for no expiration, 0 to create normal bin).
 * \return         - as_hashmap.
 */
as_hashmap 
create_bin_map(char* bin_name, char* val, int64_t bin_ttl) 
{
	as_hashmap map;
	as_hashmap_init(&map, 1);
	as_stringmap_set_str((as_map *) &map, "bin", bin_name);
	as_stringmap_set_str((as_map *) &map, "val", val);
	as_stringmap_set_int64((as_map *) &map, "bin_ttl", bin_ttl);

	return map;
}

//==========================================================
// Helpers
//

// Register a UDF function in the database.

bool
register_udf(aerospike* p_as, const char* udf_file_path)
{
	FILE* file = fopen(udf_file_path, "r");

	if (! file) {
		// If we get here it's likely that we're not running the example from
		// the right directory - the specific example directory.
		LOG("cannot open script file %s : %s", udf_file_path, strerror(errno));
		return false;
	}

	// Read the file's content into a local buffer.

	uint8_t* content = (uint8_t*)malloc(1024 * 1024);

	if (! content) {
		LOG("script content allocation failed");
		return false;
	}

	uint8_t* p_write = content;
	int read = (int)fread(p_write, 1, 512, file);
	int size = 0;

	while (read) {
		size += read;
		p_write += read;
		read = (int)fread(p_write, 1, 512, file);
	}

	fclose(file);

	// Wrap the local buffer as an as_bytes object.
	as_bytes udf_content;
	as_bytes_init_wrap(&udf_content, content, size, true);

	as_error err;
	as_string base_string;
	const char* base = as_basename(&base_string, udf_file_path);

	// Register the UDF file in the database cluster.
	if (aerospike_udf_put(p_as, &err, NULL, base, AS_UDF_TYPE_LUA,
			&udf_content) == AEROSPIKE_OK) {
		// Wait for the system metadata to spread to all nodes.
		aerospike_udf_put_wait(p_as, &err, NULL, base, 100);
	}
	else {
		LOG("aerospike_udf_put() returned %d - %s", err.code, err.message);
	}

	as_string_destroy(&base_string);

	// This frees the local buffer.
	as_bytes_destroy(&udf_content);

	return err.code == AEROSPIKE_OK;
}

// Remove the record from database, and disconnect from cluster.

void
cleanup(aerospike* as, as_error* err, as_policy_remove* policy, as_key* testKey)
{
	// Clean up the database. Note that with database "storage-engine device"
	// configurations, this record may come back to life if the server is re-
	// started. That's why this example that want to start clean removes the 
	// record at the beginning.
	
	// Remove the record from the database.
	aerospike_key_remove(as, err, NULL, testKey);

	// Disconnect from the database cluster and clean up the aerospike object.
	aerospike_close(as, err);
	aerospike_destroy(as);
}