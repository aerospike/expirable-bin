/*
 * Copyright 2012-2015 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements WHICH ARE COMPATIBLE WITH THE APACHE LICENSE, VERSION 2.0.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import com.aerospike.client.AerospikeClient;
import com.aerospike.client.AerospikeException;
import com.aerospike.client.Key;
import com.aerospike.client.Language;
import com.aerospike.client.Record;
import com.aerospike.client.Value;
import com.aerospike.client.Value.MapValue;
import com.aerospike.client.policy.Policy;
import com.aerospike.client.policy.WritePolicy;
import com.aerospike.client.query.Statement;
import com.aerospike.client.task.ExecuteTask;
import com.aerospike.client.task.RegisterTask;

public class ExpireBin {
	private static final String GET_OP          = "get";
	private static final String PUT_OP          = "put";
	private static final String BATCH_PUT_OP    = "puts";
	private static final String TOUCH_OP        = "touch";
	private static final String CLEAN_OP        = "clean";
	private static final String TTL_OP          = "ttl";
	private static final String MODULE_NAME     = "expire_bin";
	private static final String BIN_NAME_FIELD  = "bin";
	private static final String BIN_VALUE_FIELD = "val";
	private static final String BIN_TTL_FIELD   = "bin_ttl";
	
	private static AerospikeClient client;

	/**
	 * Initialize ExpireBin object with suitable client and policy.
	 * 
	 * @param client - Client to perform operations on.
	 */
	public ExpireBin(AerospikeClient client) {
		ExpireBin.client = client;
	}

	/**
	 * Try to get values from the expire bin.
	 * 
	 * @param policy - Configuration parameters for op.
	 * @param key    - Key to get from.
	 * @param bins   - List of bin names to attempt to get from.
	 * @return       - Record containing respective values for bins that haven't 
	 *                 expired/exist. The 'gen' and 'exp' numbers on the Record are not valid.
	 * @throws       - AerospikeException.
	 */
	public Object get(Policy policy, Key key, String ... bins) throws AerospikeException {
		Value[] valueBins = new Value[bins.length];
		int count = 0;
		
		for (String bin : bins) {
			valueBins[count] = Value.get(bin);
			count++;
		}
		
		Object returnVal = client.execute(policy, key, MODULE_NAME, GET_OP, valueBins);
		
		if (returnVal instanceof Map) {
			Map<?, ?> returnMap = (Map<?, ?>) returnVal;
			HashMap<String, Object> recMap = new HashMap<String, Object>();
			if (returnMap != null && !returnMap.isEmpty()) {
				for (String bin : bins) {
					if (returnMap.get(bin) != null) {
						recMap.put(bin, returnMap.get(bin));
					}
				}
				return new Record(recMap, null, 0, 0);
			}
		}
		return null;
	}

	/**
	 * Create or update expire bins. If the binTTL is not null, all newly created bins will be expire  
	 * bin, otherwise, only normal bins will be created.
	 * Note: Existing expire bins will not be converted into normal bins if binTTL is not specified.
	 * 
	 * @param policy  - Configuration parameters for op.
	 * @param key     - Record key to apply operation on.
	 * @param binName - Bin name to create or update.
	 * @param val     - Bin value.
	 * @param binTTL  - Expiration time in seconds or -1 for no expiration.
	 * @return        - 0 if success, 1 if error.
	 * @throws        - AerospikeException.
	 */
	public Integer put(Policy policy, Key key, String binName, Value val, int binTTL) throws AerospikeException {
		return (Integer) client.execute(policy, key, MODULE_NAME, PUT_OP, Value.get(binName), val, Value.get(binTTL));
	}

	/**
	 * Batch create or update expire bins for a given key. Use the createBinMap method
	 * to create each put operation. Supply a binTTL of 0 to turn bin creation off.  
	 * 
	 * @param policy  - Configuration parameters for op.
	 * @param key     - Record key to store bins.
	 * @param mapBins - List of Maps generated by createBinMap containing operation arguments.
	 * @return        - 0 if all bins succeeded, 1 if failure.
	 * @throws        - AerospikeException.
	 */
	public Integer puts(Policy policy, Key key, MapValue ... mapBins) throws AerospikeException {
		return (Integer) client.execute(policy, key, MODULE_NAME, BATCH_PUT_OP, (Value[]) mapBins);
	}

	/**
	 * Batch update the bin TTLs. Use this method to change or reset 
	 * the bin TTL of multiple bins in a record. 
	 * 
	 * @param policy  - Configuration parameters for op.
	 * @param key     - Record key.
	 * @param mapBins - List of MapValues generated by createMapBin containing operation arguments.
	 * @return        - 0 on success of all touch operations, 1 if a failure occurs.
	 * @throws        - AerospikeException.
	 */
	public Integer touch(Policy policy, Key key, MapValue ... mapBins) throws AerospikeException {
		for (Value.MapValue map : mapBins) {
			@SuppressWarnings("unchecked")
			Map<String, Object> temp_map = (Map<String, Object>) map.getObject();
			if (temp_map.get(BIN_TTL_FIELD) == null) {
				throw new AerospikeException("TTL not specified");
			}
		}
		return (Integer) client.execute(policy, key, MODULE_NAME, TOUCH_OP, (Value[]) mapBins);
	}

	/**
	 * Perform a scan of the database and clear out expired bins.
	 * 
	 * @param policy    - Configuration parameters for op.
	 * @param scan      - Scan policy containing which records should be scanned.
	 * @param namespace - Namespace of server to scan.
	 * @param set       - Set of server to scan.
	 * @param bins      - List of bins to scan.
	 * @throws          - AerospikeException.
	 */
	public ExecuteTask clean(Policy policy, Statement statement, String ... bins) throws AerospikeException {
		final Value[] valueBins = new Value[bins.length];
		int count = 0;
		for (String bin : bins) {
			valueBins[count] = Value.get(bin);
			count++;
		}
		return client.execute(policy, statement, MODULE_NAME, CLEAN_OP, valueBins);
	}

	/**
	 * Get bin TTL in seconds.
	 * 
	 * @param policy - Configuration parameters for op.
	 * @param key    - Record key.
	 * @param bin    - Bin Name.
	 * @return       - Time in seconds bin will expire, -1 or null if it doesn't expire.
	 * @throws       - AerospikeException. 
	 */
	public Integer ttl(Policy policy, Key key, String bin) throws AerospikeException {
		return (Integer) (client.execute(policy, key, MODULE_NAME, TTL_OP, Value.get(bin)));
	}
	
	/**
	 * Used to generate maps for use with batch put and touch operations.
	 * 
	 * @param binName - Name of bin to perform op on.
	 * @param val     - Value to insert into bin (PUT OP ONLY).
	 * @param binTTL  - Bin ttl for bin (-1 for no expiration, 0 to create normal bin).
	 * @return        - The generated map.
	 * @throws        - AerospikeException.
	 */
	public static MapValue createBinMap(String binName, Value val, int binTTL) throws AerospikeException {
		HashMap<String, Object> rm = new HashMap<String, Object>();
		if (binName != null) {
			rm.put(BIN_NAME_FIELD, binName);
		} else {
			throw new AerospikeException("No bin name specified.");
		}
		
		if (val != null) {
			rm.put(BIN_VALUE_FIELD, val);
		}
		rm.put (BIN_TTL_FIELD, binTTL);
		return new MapValue(rm);
	}
	
	public static void main(String[] args) throws Exception {
		AerospikeClient testClient = null;
		System.out.println("This is a demo of the expirable bin module for Java:");
		try {
			System.out.println("\nConnecting to Aerospike server...");
			testClient = new AerospikeClient("127.0.0.1", 3000);
			System.out.println("Connected!");
			Policy policy = new WritePolicy();
			System.out.println("\nRegistering UDF...");
			try {
				RegisterTask regStatus = testClient.register(policy, "expire_bin.lua", "expire_bin.lua", Language.LUA);
				if (regStatus.isDone()) {
					System.out.println("UDF registered!");
				}else {
					throw new AerospikeException("UDF could not be registered!");
				}
			} catch (Exception e) {
				System.out.println("Error registering UDF: " + e.toString());
			}
			
			ExpireBin eb = new ExpireBin(testClient);
			Key testKey = new Key("test", "expireBin", "eb");

			// Example 1: validates the basic bin expiration.
			expExample(policy, testKey, eb);
			
			// Example 2: validates the basic bin expiration after using 'touch'.
			touchExample(policy, testKey, eb);
			
			// Example 3: shows the difference between normal 'get' and 'eb.get'.
			getExample(policy, testKey, eb);
			
			System.out.println("Demo of the expirable bin module for Java successfully completed");
		} catch (AerospikeException e) {
			e.printStackTrace();
			System.exit(1);
		}
	}
	
	private static void expExample(Policy policy, Key testKey, ExpireBin eb) throws AerospikeException {
		System.out.println("\nInserting bins...");
		System.out.println(eb.put(policy, testKey, "TestBin1", Value.get("Hello World."), -1) == 0 ? "TestBin 1 inserted" : "TestBin 1 not inserted");
		System.out.println(eb.put(policy, testKey, "TestBin2", Value.get("I don't expire."), 8) == 0 ? "TestBin 2 inserted" : "TestBin 2 not inserted");
		System.out.println(eb.put(policy, testKey, "TestBin3", Value.get("I will expire soon."), 5) == 0 ? "TestBin 3 inserted" : "TestBin 3 not inserted");
		
		System.out.println("Getting bins...");
		System.out.println(eb.get(policy, testKey, "TestBin1", "TestBin2", "TestBin3"));
		
		System.out.println("Getting bins TTL...");
		System.out.println("TestBin 1 TTL: " + eb.ttl(policy, testKey, "TestBin1"));
		System.out.println("TestBin 2 TTL: " + eb.ttl(policy, testKey, "TestBin2"));
		System.out.println("TestBin 3 TTL: " + eb.ttl(policy, testKey, "TestBin3"));
		
		System.out.println("Waiting for TestBin 3 to expire...");
		try {
			TimeUnit.SECONDS.sleep(6);
		} catch(InterruptedException ex) {
			Thread.currentThread().interrupt();
		}
		
		System.out.println("Getting bins again...");
		System.out.println(eb.get(policy, testKey, "TestBin1", "TestBin2", "TestBin3"));
	}
	
	private static void touchExample(Policy policy, Key testKey, ExpireBin eb) throws AerospikeException {
		System.out.println("\nChanging expiration time for TestBin 1 and TestBin 2...");
		eb.touch(policy, testKey, createBinMap("TestBin1", null, 3), createBinMap("TestBin2", null, -1));
		
		System.out.println("Getting bins TTL...");
		System.out.println("TestBin 1 TTL: " + eb.ttl(policy, testKey, "TestBin1"));
		System.out.println("TestBin 2 TTL: " + eb.ttl(policy, testKey, "TestBin2"));
		
		System.out.println("Waiting for TestBin 1 to expire...");
		try {
			TimeUnit.SECONDS.sleep(4);
		} catch(InterruptedException ex) {
			Thread.currentThread().interrupt();
		}
		
		System.out.println("Getting bins again...");
		System.out.println(eb.get(policy, testKey, "TestBin1", "TestBin2", "TestBin3"));
	}
	
	private static void getExample(Policy policy, Key testKey, ExpireBin eb) throws Exception {
		// This illustrates the use of 'puts'.
		System.out.println("\nInserting bins...");
		System.out.println(eb.puts(policy, testKey, 
				createBinMap("TestBin4", Value.get("Good Morning."), 5), 
				createBinMap("TestBin5", Value.get("Good Night."), 5)) == 0 ? "TestBin 4 & 5 inserted" : "TestBin 4 & 5 not inserted");

		System.out.println("Sleeping for 6 seconds (TestBin 4 and TestBin 5 will expire)...");
		Thread.sleep(6 * 1000);
		
		// Read the record using 'eb.get' after it expires, showing it's gone.
		System.out.println("Getting TestBin 4 and Testbin 5 using 'eb interface'...");
		System.out.println(eb.get(policy, testKey, "TestBin4", "TestBin5"));
		
		// Read the record using normal 'get' after it expires, showing it's persistent.
		System.out.println("Getting TestBin 4 and TestBin 5 using 'normal get'...");
		Record record;
		record = client.get(policy, testKey, "TestBin4", "TestBin5");
		if (record == null) {
			System.out.println("Record not found");
		} else {
			System.out.println(record.toString());
		}
		
		System.out.println("Cleaning bins...");
		Statement stmt = new Statement();
		stmt.setNamespace("test");
		stmt.setSetName("expireBin");
		ExecuteTask task = eb.clean(policy, stmt, "TestBin1", "TestBin2", "TestBin3", "TestBin4", "TestBin5");
		
		while (!task.isDone()) {
			System.out.println("Scan in progress...");
			try {
				TimeUnit.SECONDS.sleep(5);
			} catch (InterruptedException ex) {
				Thread.currentThread().interrupt();
			}
		}
		
		System.out.println("Scan completed!");
		
		System.out.println("Checking expire bins again using 'eb interface'...");
		System.out.println(eb.get(policy, testKey, "TestBin1", "TestBin2", "TestBin3", "TestBin4", "TestBin5"));
		
		System.out.println("Checking expire bins again using 'normal get'...");
		record = client.get(policy, testKey, "TestBin1", "TestBin2", "TestBin3", "TestBin4", "TestBin5");
		
		if (record != null) {
			System.out.println(record.toString() + "\n");
		}
		else {		
			System.out.println("Record not found\n");
		}
	}
}