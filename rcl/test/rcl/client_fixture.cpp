// Copyright 2016 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <string>
#include <thread>

#include "rcutils/logging_macros.h"

#include "rcl/client.h"
#include "rcl/rcl.h"

#include "example_interfaces/srv/add_two_ints.h"

#include "../memory_tools/memory_tools.hpp"
#include "../scope_exit.hpp"
#include "rcl/error_handling.h"
#include "rcl/graph.h"

bool
wait_for_server_to_be_available(
  rcl_node_t * node,
  rcl_client_t * client,
  size_t max_tries,
  int64_t period_ms)
{
  size_t iteration = 0;
  do {
    ++iteration;
    bool is_ready;
    rcl_ret_t ret = rcl_service_server_is_available(node, client, &is_ready);
    if (ret != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(
        ROS_PACKAGE_NAME,
        "Error in rcl_service_server_is_available: %s",
        rcl_get_error_string_safe())
      return false;
    }
    if (is_ready) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
  } while (iteration < max_tries);
  return false;
}

bool
wait_for_client_to_be_ready(
  rcl_client_t * client,
  size_t max_tries,
  int64_t period_ms)
{
  rcl_wait_set_t wait_set = rcl_get_zero_initialized_wait_set();
  rcl_ret_t ret = rcl_wait_set_init(&wait_set, 0, 0, 0, 1, 0, rcl_get_default_allocator());
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED(
      ROS_PACKAGE_NAME, "Error in wait set init: %s", rcl_get_error_string_safe())
    return false;
  }
  auto wait_set_exit = make_scope_exit(
    [&wait_set]() {
      if (rcl_wait_set_fini(&wait_set) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR_NAMED(
          ROS_PACKAGE_NAME, "Error in wait set fini: %s", rcl_get_error_string_safe())
        throw std::runtime_error("error while waiting for client");
      }
    });
  size_t iteration = 0;
  do {
    ++iteration;
    if (rcl_wait_set_clear_clients(&wait_set) != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(
        ROS_PACKAGE_NAME, "Error in wait_set_clear_clients: %s", rcl_get_error_string_safe())
      return false;
    }
    if (rcl_wait_set_add_client(&wait_set, client) != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(
        ROS_PACKAGE_NAME, "Error in wait_set_add_client: %s", rcl_get_error_string_safe())
      return false;
    }
    ret = rcl_wait(&wait_set, RCL_MS_TO_NS(period_ms));
    if (ret == RCL_RET_TIMEOUT) {
      continue;
    }
    if (ret != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(ROS_PACKAGE_NAME, "Error in wait: %s", rcl_get_error_string_safe())
      return false;
    }
    for (size_t i = 0; i < wait_set.size_of_clients; ++i) {
      if (wait_set.clients[i] && wait_set.clients[i] == client) {
        return true;
      }
    }
  } while (iteration < max_tries);
  return false;
}

int main(int argc, char ** argv)
{
  int main_ret = 0;
  {
    if (rcl_init(argc, argv, rcl_get_default_allocator()) != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(
        ROS_PACKAGE_NAME, "Error in rcl init: %s", rcl_get_error_string_safe())
      return -1;
    }
    rcl_node_t node = rcl_get_zero_initialized_node();
    const char * name = "client_fixture_node";
    rcl_node_options_t node_options = rcl_node_get_default_options();
    if (rcl_node_init(&node, name, "", &node_options) != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(
        ROS_PACKAGE_NAME, "Error in node init: %s", rcl_get_error_string_safe())
      return -1;
    }
    auto node_exit = make_scope_exit(
      [&main_ret, &node]() {
        if (rcl_node_fini(&node) != RCL_RET_OK) {
          RCUTILS_LOG_ERROR_NAMED(
            ROS_PACKAGE_NAME, "Error in node fini: %s", rcl_get_error_string_safe())
          main_ret = -1;
        }
      });

    const rosidl_service_type_support_t * ts = ROSIDL_GET_SRV_TYPE_SUPPORT(
      example_interfaces, AddTwoInts);
    const char * topic = "add_two_ints";

    rcl_client_t client = rcl_get_zero_initialized_client();
    rcl_client_options_t client_options = rcl_client_get_default_options();
    rcl_ret_t ret = rcl_client_init(&client, &node, ts, topic, &client_options);
    if (ret != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(
        ROS_PACKAGE_NAME, "Error in client init: %s", rcl_get_error_string_safe())
      return -1;
    }

    auto client_exit = make_scope_exit(
      [&client, &main_ret, &node]() {
        if (rcl_client_fini(&client, &node)) {
          RCUTILS_LOG_ERROR_NAMED(
            ROS_PACKAGE_NAME, "Error in client fini: %s", rcl_get_error_string_safe())
          main_ret = -1;
        }
      });

    // Wait until server is available
    if (!wait_for_server_to_be_available(&node, &client, 1000, 100)) {
      RCUTILS_LOG_ERROR_NAMED(ROS_PACKAGE_NAME, "Server never became available")
      return -1;
    }

    // Initialize a request.
    example_interfaces__srv__AddTwoInts_Request client_request;
    example_interfaces__srv__AddTwoInts_Request__init(&client_request);
    client_request.a = 1;
    client_request.b = 2;
    int64_t sequence_number;

    if (rcl_send_request(&client, &client_request, &sequence_number)) {
      RCUTILS_LOG_ERROR_NAMED(
        ROS_PACKAGE_NAME, "Error in send request: %s", rcl_get_error_string_safe())
      return -1;
    }

    if (sequence_number != 1) {
      RCUTILS_LOG_ERROR_NAMED(ROS_PACKAGE_NAME, "Got invalid sequence number")
      return -1;
    }

    example_interfaces__srv__AddTwoInts_Request__fini(&client_request);

    // Initialize the response owned by the client and take the response.
    example_interfaces__srv__AddTwoInts_Response client_response;
    example_interfaces__srv__AddTwoInts_Response__init(&client_response);

    if (!wait_for_client_to_be_ready(&client, 1000, 100)) {
      RCUTILS_LOG_ERROR_NAMED(ROS_PACKAGE_NAME, "Client never became ready")
      return -1;
    }
    rmw_request_id_t header;
    if (rcl_take_response(&client, &header, &client_response) != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED(
        ROS_PACKAGE_NAME, "Error in send response: %s", rcl_get_error_string_safe())
      return -1;
    }

    example_interfaces__srv__AddTwoInts_Response__fini(&client_response);
  }

  return main_ret;
}
