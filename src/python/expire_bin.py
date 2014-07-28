import aerospike

MODULE_NAME = "expire_bin"
GET_OP = "get"
PUT_OP = "put"
BATCH_PUT_OP = "puts"
TTL_OP = "ttl"
TOUCH_OP = "touch"
CLEAN_OP = "clean"

class ExpireBin:
	def __init__(self, client, policy):
		"""Initialize the ExpireBin module

		Args:
			client -- aerospike client to perform ops
			policy -- policy to use for ops
		"""

		self.client = client
		self.policy = policy

	def get(self, key, *bins):
		"""Attempt to retrieve values from list of bins. The bins
		can be expire bins or normal bins.

		Args:
			key -- tuple (namespace, set, record name)
			*bins -- one or more bin names to retrieve values from

		Returns:
			list: A list with each value respective to the list of bin
			names passed in. If a bin is expired or empty, the index
			in the list will contain None.

		Raises:
			Exception: Exception with details of server error.
		"""
		try:
			return self.client.apply(key, MODULE_NAME, GET_OP, list(bins), self.policy)
		except Exception as e:
			raise

	def put(self, key, bin, val, bin_ttl, create):
		"""Create or update expire bins. If the create flag is set to true,
		all newly created bins will be expire bins otherwise, only normal bins will be created
		and existing expire bins will be updated. Note: existing expire bins will not be converted
		into normal bins if create flag is off.

		Args:
			key -- tuple (namespace, set, record name)
			bin -- bin name
			val -- value to store into bin
			bin_ttl -- expiration time in seconds or -1 for no expiration
			create -- bool, set to true to create expire bins, false to create normal bins

		Returns:
			int: 0 if success, 1 otherwise
		"""
		if create:
			create = 0
		else:
			create = 1
		try:
			return self.client.apply(key, MODULE_NAME, PUT_OP, [bin, val, bin_ttl, create], self.policy)
		except Exception as e:
			raise

		def puts(self, key, *binMaps):
			"""Batch create or update expire bins for a given key. Use the dict
				{'bin' : bin_name, 'val' : bin_value, 'bin_ttl' : ttl}
			to store each put operation. Omit the bin_ttl to turn bin creation off.

			Args:
				key -- tuple (namespace, set, record name)
				*binMaps -- dict {'bin' : bin_name, 'val' : bin_val, 'bin_ttl' : ttl}

			Returns:
				int: 0 if all ops succeeded, 1 if error occurred

			"""
			try:
				return self.client.apply(key, MODULE_NAME, BATCH_PUT_OP, list(binMaps), self.policy)
			except Exception as e:
				raise

		def touch(key, *mapBins):
			"""Batch update the bin TTLs. Us this method to change or reset the bin TTL of
			multiple bins in a record. Use a dict to store each touch operation.

			Args:
				key -- tuple (namespace, set, record name)
				*mapBins -- dict {'bin' : bin_name, 'bin_ttl' : ttl}

			Returns:
				int: 0 on success of ALL touch operations, 1 if a failure occurs
			"""
			try:
				return self.client.apply(key, MODULE_NAME, TOUCH_OP, list(mapBins), self.policy)
			except Exception as e:
				raise

		def clean(scan, *bins):
			"""CLear out the expired bins on a scan of the database

			Args:
				scan -- Scan object to run bin clean on
				*bins -- bin names to clean out
			"""
			def clean_call((key, metadata, record)):
				try:
					self.client.apply(key, MODULE_NAME, CLEAN_OP, list(bins), self.policy)
				except Exception as e:
					raise

			scan.foreach(clean_call)

def main():
	config = { 'hosts' : [ ('127.0.0.1', 3000) ]}
	policy = { "timeout" : 2000 }
	testClient = aerospike.client(config).connect()
	eb = ExpireBin(testClient, policy)
	key = ("test", "expireBin", "eb1")


if __name__ == "__main__":
	main()


