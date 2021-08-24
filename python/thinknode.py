"""This file provides utilities for working with Thinknode."""

import sys
import json
import msgpack
import yaml
import requests
import websocket
import uuid
from array import array


def union_tag(obj):
    """Get the tag of a union object."""
    return next(iter(obj))


def raise_(ex):
    """Raise the specified exception.

    This allows exceptions to be raised from expressions."""
    raise ex


def get_service_id(id):
    """Get the service associated with the given Thinknode ID."""
    a = array('H', bytes.fromhex(id))
    if sys.byteorder == "little":
        a.byteswap()
    type_code = (a[2] & 0x0ff0) >> 6
    type_mapping = {
        1: 'iam',
        2: 'apm',
        3: 'iss',
        4: 'calc',
        5: 'cas',
        6: 'rks',
        7: 'immutable'
    }
    return type_mapping.get(type_code, 'unknown')


class Session:
    """This represents a Thinknode session."""

    def __init__(self):
        with open('config.yml') as config_file:
            self.config = yaml.full_load(config_file)
        self.headers = {
            'Authorization': 'Bearer ' + self.config["api_token"],
            'Content-Type': 'application/json',
            'Accept': 'application/json'
        }
        self.realm = self.config["realm_name"]

        # Connect to the CRADLE server.
        self.ws = websocket.create_connection(self.config["cradle_url"])
        self.ws.send_binary(
            msgpack.packb(
                {
                    "request_id": "no_id",
                    "content": {
                        "registration": {
                            "name": "thinknode.python.lib",
                            "session": {
                                "api_url": self.config["api_url"],
                                "access_token": self.config["api_token"]
                            }
                        }
                    }
                },
                use_bin_type=True))

        self.retrieve_realm_context()

    def retrieve_realm_context(self):
        """Get the context associated with the app in the config file."""
        url = self.config["api_url"] + "/iam/realms/" + self.realm + "/context"
        response = requests.get(url, headers=self.headers)
        response.raise_for_status()
        self.context = response.json()["id"]

    def realm_context(self):
        return self.context

    def get(self, path):
        """Do a GET request."""
        url = self.config["api_url"] + path
        response = requests.get(url, headers=self.headers)
        response.raise_for_status()
        try:
            return response.json()
        except:
            return None

    def get_raw(self, path):
        """Do a GET request but don't try to parse the output."""
        url = self.config["api_url"] + path
        response = requests.get(url, headers=self.headers)
        response.raise_for_status()
        return response.text

    def patch(self, path, content):
        """Do a PATCH request."""
        url = self.config["api_url"] + path
        response = requests.patch(url, headers=self.headers, data=content)
        response.raise_for_status()

    def put(self, path, content=None):
        """Do a PUT request."""
        url = self.config["api_url"] + path
        response = requests.put(url, headers=self.headers, data=content)
        response.raise_for_status()

    def post(self, path, content):
        """Do a POST request."""
        url = self.config["api_url"] + path
        response = requests.post(url, headers=self.headers, data=content)
        response.raise_for_status()
        try:
            return response.json()
        except:
            return None

    def delete(self, path):
        """Do a DELETE request."""
        url = self.config["api_url"] + path
        response = requests.delete(url, headers=self.headers)
        response.raise_for_status()

    def get_iss_object(self, object_id, context=None, ignore_upgrades=False):
        """Retrieve an object from the ISS."""
        print("FETCHING " + object_id, file=sys.stderr)
        if context is None:
            context = self.realm_context()
        request_id = uuid.uuid4().hex
        self.ws.send_binary(
            msgpack.packb(
                {
                    "request_id": request_id,
                    "content": {
                        "iss_object": {
                            "context_id": context,
                            "object_id": object_id,
                            "encoding": "msgpack",
                            "ignore_upgrades": ignore_upgrades
                        }
                    }
                },
                use_bin_type=True))
        response = msgpack.unpackb(self.ws.recv(), use_list=False, raw=False)
        if response["request_id"] != request_id:
            print("mismatched request IDs")
            sys.exit(1)
        if union_tag(response["content"]) == "error":
            print(json.dumps(response, indent=4))
            sys.exit(1)
        gio = response["content"]["iss_object_response"]
        return msgpack.unpackb(gio["object"], use_list=False, raw=False)

    def get_iss_object_metadata(self, object_id, context=None):
        """Retrieve an ISS object's metadata."""
        if context is None:
            context = self.realm_context()
        request_id = uuid.uuid4().hex
        self.ws.send_binary(
            msgpack.packb(
                {
                    "request_id": request_id,
                    "content": {
                        "iss_object_metadata": {
                            "request_id": request_id,
                            "context_id": context,
                            "object_id": object_id
                        }
                    }
                },
                use_bin_type=True))
        response = msgpack.unpackb(self.ws.recv(), use_list=False, raw=False)
        if response["request_id"] != request_id:
            print("mismatched request IDs")
            sys.exit(1)
        if union_tag(response["content"]) == "error":
            print(json.dumps(response, indent=4))
            sys.exit(1)
        giom = response["content"]["iss_object_metadata_response"]
        return giom["metadata"]

    def resolve_meta_chain(self, generator_calc, context=None):
        """Retrieve an object from the ISS."""
        print("RESOLVING META CHAIN", file=sys.stderr)
        if context is None:
            context = self.realm_context()
        request_id = uuid.uuid4().hex
        self.ws.send_binary(
            msgpack.packb(
                {
                    "request_id": request_id,
                    "content": {
                        "resolve_meta_chain": {
                            "context_id": context,
                            "generator": generator_calc
                        }
                    }
                },
                use_bin_type=True))
        response = msgpack.unpackb(self.ws.recv(), use_list=False, raw=False)
        if response["request_id"] != request_id:
            print("mismatched request IDs")
            sys.exit(1)
        if union_tag(response["content"]) == "error":
            print(json.dumps(response, indent=4))
            sys.exit(1)
        rmc = response["content"]["resolve_meta_chain_response"]
        return rmc["calculation_id"]

    def wait_for_calc(self, calc_id, context=None):
        """Wait for a calculation to finish."""
        print("WAITING FOR " + calc_id, file=sys.stderr)
        if context is None:
            context = self.realm_context()
        while True:
            status = \
                self.get("/calc/" + calc_id + "/status?context=" +
                         context + "&status=completed")
            if union_tag(status) == "failed":
                print(json.dumps(status, indent=4), file=sys.stderr)
                sys.exit(1)
            if union_tag(status) == "completed":
                break

    def post_iss_object(self, schema, obj, context=None):
        """Post an ISS object and return its ID."""
        print("POSTING ISS OBJECT", file=sys.stderr)
        if context is None:
            context = self.realm_context()
        request_id = uuid.uuid4().hex
        self.ws.send_binary(
            msgpack.packb(
                {
                    "request_id": request_id,
                    "content": {
                        "post_iss_object": {
                            "context_id": context,
                            "schema": schema,
                            "object": msgpack.packb(obj, use_bin_type=True),
                            "encoding": "msgpack"
                        }
                    }
                },
                use_bin_type=True))
        response = msgpack.unpackb(self.ws.recv(), use_list=False, raw=False)
        if response["request_id"] != request_id:
            print("mismatched request IDs")
            sys.exit(1)
        if union_tag(response["content"]) == "error":
            print(json.dumps(response, indent=4))
            sys.exit(1)
        pio = response["content"]["post_iss_object_response"]
        return pio["object_id"]

    def copy_iss_object(self, source_bucket, object_id, context=None):
        """Copy an ISS object from another bucket."""
        print("COPYING ISS OBJECT", file=sys.stderr)
        if context is None:
            context = self.realm_context()
        request_id = uuid.uuid4().hex
        self.ws.send_binary(
            msgpack.packb(
                {
                    "request_id": request_id,
                    "content": {
                        "copy_iss_object": {
                            "source_bucket": source_bucket,
                            "destination_context_id": context,
                            "object_id": object_id
                        }
                    }
                },
                use_bin_type=True))
        response = msgpack.unpackb(self.ws.recv(), use_list=False, raw=False)
        if response["request_id"] != request_id:
            print("mismatched request IDs")
            sys.exit(1)
        if union_tag(response["content"]) == "error":
            print(json.dumps(response, indent=4))
            sys.exit(1)
        pio = response["content"]["copy_iss_object_response"]
        if pio["request_id"] != request_id:
            print("mismatched request IDs")
            sys.exit(1)

    def post_calc(self, calc, context=None):
        """Post a calculation and return its ID."""
        print("POSTING CALC", file=sys.stderr)
        if context is None:
            context = self.realm_context()
        request_id = uuid.uuid4().hex
        self.ws.send_binary(
            msgpack.packb(
                {
                    "request_id": request_id,
                    "content": {
                        "post_calculation": {
                            "context_id": context,
                            "calculation": calc
                        }
                    }
                },
                use_bin_type=True))
        response = msgpack.unpackb(self.ws.recv(), use_list=False, raw=False)
        if response["request_id"] != request_id:
            print("mismatched request IDs")
            sys.exit(1)
        if union_tag(response["content"]) == "error":
            print(json.dumps(response, indent=4))
            sys.exit(1)
        pc = response["content"]["post_calculation_response"]
        return pc["calculation_id"]

    def get_calc_request(self, calc_id, context=None):
        """Get the request associated with the given calculation ID."""
        print("GETTING CALC " + calc_id, file=sys.stderr)
        if context is None:
            context = self.realm_context()
        request_id = uuid.uuid4().hex
        self.ws.send_binary(
            msgpack.packb(
                {
                    "request_id": request_id,
                    "content": {
                        "calculation_request": {
                            "context_id": context,
                            "calculation_id": calc_id
                        }
                    }
                },
                use_bin_type=True))
        response = msgpack.unpackb(self.ws.recv(), use_list=False, raw=False)
        if response["request_id"] != request_id:
            print("mismatched request IDs")
            sys.exit(1)
        if union_tag(response["content"]) == "error":
            print(json.dumps(response, indent=4))
            sys.exit(1)
        gcr = response["content"]["calculation_request_response"]
        return gcr["calculation"]

    def substitute_calc(self, parent_calc_id, old_id, new_id, context=None):
        """Substitute all instances of an old ID within a parent calc with a new ID."""

        def substitute_in_calc(calc):
            """Apply the old_id->new_id substitution to a calculation request."""
            # These are all the cases for handling different types of calcs.
            cases = {
                "array":
                    lambda array: {
                        **array, 'items': list(map(substitute_in_calc, array['items']))},
                "cast":
                    lambda cast: {
                        **cast, 'object': substitute_in_calc(cast['object'])},
                "function":
                    lambda fun: {
                        **fun, 'args': list(map(substitute_in_calc, fun['args']))},
                "item":
                    lambda item: {
                        **item,
                        'array': substitute_in_calc(item['array']),
                        'index': substitute_in_calc(item['index'])
                    },
                "let":
                    lambda let: {**let, 'in': substitute_in_calc(let['in'])},
                "meta":
                    lambda _: raise_(ValueError(
                        "can't substitute within meta")),
                "object":
                    lambda obj: {
                        **obj,
                        'properties':
                            {k: substitute_in_calc(
                                v) for k, v in obj['properties'].items()}
                    },
                "property":
                    lambda prop: {
                        **prop,
                        'object': substitute_in_calc(prop['object']),
                        'field': substitute_in_calc(prop['field'])
                    },
                "reference": substitute_in_ref,
                "value": lambda val: val,
                "variable": lambda var: var,
            }
            # Check the tag of the schema and invoke the appropriate case.
            tag = union_tag(calc)
            return {tag: cases[tag](calc[tag])}

        result_cache = {}

        def substitute_in_ref(calc_id):
            """Apply the old_id->new_id substitution to a referenced calculation."""
            # Check if this is the ID we're trying to substitute for.
            if calc_id == old_id:
                return new_id
            # It's only possible to apply substitutions within calculations.
            if get_service_id(calc_id) != 'calc':
                return calc_id
            # Check the cache.
            if calc_id in result_cache:
                return result_cache[calc_id]
            # Otherwise, get the actual calculation request and substitute within that.
            original_calc = self.get_calc_request(calc_id, context=context)
            substituted_calc = substitute_in_calc(original_calc)
            # If this resulted in a different calculation, post it.
            if substituted_calc != original_calc:
                substituted_calc_id = self.post_calc(
                    substituted_calc, context=context)
            else:
                substituted_calc_id = calc_id
            # And record the result in the cache.
            result_cache[calc_id] = substituted_calc_id
            return substituted_calc_id

        return substitute_in_ref(parent_calc_id)
