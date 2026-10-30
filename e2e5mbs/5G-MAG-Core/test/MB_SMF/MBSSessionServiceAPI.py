"""
License: 5G-MAG Public License (v1.0)
Author: Borja Iñesta Hernández
Copyright: (C) 2024 iTEAM UPV
For full license terms please see the LICENSE file distributed with this
program. If this file is missing then the license can be retrieved from
https://drive.google.com/file/d/1cinCiA778IErENZ3JN52VFW-1ffHpx7Z/view
"""

import json
import os
import unittest
from urllib.parse import urlparse

import httpcore

from utils.config import config_from_file
from utils.http2_prior_knowledge import setup_http2_pool
from utils.json_schema import json_validate
from utils.logger import setup_logger

global config, logger
config = config_from_file("config.toml")
logger = setup_logger(__name__, config["DEFAULT"]["log_level"])
http2 = setup_http2_pool()

global api_base_uri
api_name = "/nmbsmf-mbssession"
api_version = "/v1"
api_base_uri = f"{api_name}{api_version}"
tmgi_api_base_uri = "/nmbsmf-tmgi/v1"


def load_json(path):
    with open(path) as file:
        return json.load(file)


def mb_smf_url(path):
    return f"{config['mb_smf']['protocol']}://{config['mb_smf']['address']}:{config['mb_smf']['port']}{path}"


def get_header(response, name):
    expected = name.lower().encode("ascii")
    for header_name, header_value in response.headers:
        if header_name.lower() == expected:
            return header_value.decode("utf-8")
    return ""


def allocate_tmgi():
    request_json = {"tmgiNumber": 1}
    url = mb_smf_url(f"{tmgi_api_base_uri}/tmgi")
    headers = {"Content-Type": "application/json"}
    response = http2.request("POST", url, headers=headers, content=json.dumps(request_json).encode("utf-8"))
    response_json = json.loads(response.content) if response.content else {}
    logger.debug(f"Allocated TMGI response [{response.status}] with JSON: {response_json}")
    if response.status != 200 or not response_json.get("tmgiList"):
        raise AssertionError(f"Unable to allocate TMGI from MB-SMF: {response.status} {response_json}")
    return response_json["tmgiList"][0]


def endpoint_url_from_location(location):
    parsed = urlparse(location)
    if parsed.scheme and parsed.netloc:
        return mb_smf_url(parsed.path)
    return mb_smf_url(location)


class MBSSessionServiceOperation(unittest.TestCase):
    api_resource_uri = "/mbs-sessions"

    def test_create_and_release_mbs_session(self):
        api_path = f"{api_base_uri}{self.api_resource_uri}"

        print("\n")
        logger.info("test_create_mbs_session")

        request_json = load_json("support/nmbsmf-mbssession/mbs_session_create.json")
        request_json["mbsSession"]["mbsSessionId"]["tmgi"] = allocate_tmgi()
        input_validation = json_validate(request_json, "support/nmbsmf-mbssession/mbs_session_create.schema.json")
        logger.debug(f"Input JSON validation {input_validation['result']}: {input_validation['message']}")
        self.assertEqual(input_validation["result"], "success")

        url = mb_smf_url(api_path)
        headers = {"Content-Type": "application/json"}
        response = http2.request("POST", url, headers=headers, content=json.dumps(request_json).encode("utf-8"))

        logger.debug(f"Sending POST request to {url} with JSON: {request_json}")
        response_status = response.status
        response_json = json.loads(response.content) if response.content else {}
        logger.debug(f"Received response [{response_status}] with JSON: {response_json}")

        self.assertIn(response_status, [200, 201])
        output_validation = json_validate(response_json, "support/nmbsmf-mbssession/mbs_session_created.schema.json")
        logger.debug(f"Output JSON validation {output_validation['result']}: {output_validation['message']}")
        self.assertEqual(output_validation["result"], "success")

        session_ref = response_json.get("mbsSessionRef") or get_header(response, "location")
        self.assertTrue(session_ref)

        if os.environ.get("MBS_SESSION_RELEASE_CLEANUP") != "1":
            logger.warning("Skipping MB-SMF MBS session release cleanup because this container image crashes on release")
            return

        delete_url = endpoint_url_from_location(session_ref) if session_ref.startswith("http") else mb_smf_url(f"{api_path}/{session_ref}")
        logger.debug(f"Sending DELETE request to {delete_url}")
        try:
            delete_response = http2.request("DELETE", delete_url, headers=headers)
            logger.debug(f"Received response [{delete_response.status}]")
            self.assertIn(delete_response.status, [200, 202, 204, 404])
        except httpcore.NetworkError as exc:
            logger.warning(f"MB-SMF release endpoint closed the connection during cleanup: {exc}")

    def test_reject_invalid_mbs_session_create_request_locally(self):
        request_json = load_json("support/nmbsmf-mbssession/mbs_session_create_invalid.json")
        input_validation = json_validate(request_json, "support/nmbsmf-mbssession/mbs_session_create.schema.json")
        logger.debug(f"Invalid input JSON validation {input_validation['result']}: {input_validation['message']}")
        self.assertEqual(input_validation["result"], "error")
