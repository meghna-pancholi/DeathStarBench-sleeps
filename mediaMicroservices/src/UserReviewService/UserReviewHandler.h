#ifndef MEDIA_MICROSERVICES_USERREVIEWHANDLER_H
#define MEDIA_MICROSERVICES_USERREVIEWHANDLER_H

#include <iostream>
#include <string>
#include <thread>
#include <cstdlib>
#include <future>
#include <chrono>
#include <cstring>
#include <iterator>

#include <mongoc.h>
#include <libmemcached/memcached.h>
#include <libmemcached/util.h>
#include <bson/bson.h>

#include "../../gen-cpp/UserReviewService.h"
#include "../../gen-cpp/ReviewStorageService.h"
#include "../logger.h"
#include "../tracing.h"
#include "../ClientPool.h"
#include "../RedisClient.h"
#include "../ThriftClient.h"

namespace media_service {
class UserReviewHandler : public UserReviewServiceIf {
 public:
  UserReviewHandler(
      memcached_pool_st *,
      mongoc_client_pool_t *,
      ClientPool<ReviewStorageServiceClient> *);
  ~UserReviewHandler() override = default;
  void UploadUserReview(int64_t, int64_t, int64_t, int64_t,
                         const std::map<std::string, std::string> &) override;
  void ReadUserReviews(std::vector<Review> & _return, int64_t req_id,
                       int64_t user_id, int32_t start, int32_t stop,
                        const std::map<std::string, std::string> & carrier) override;

 private:
  memcached_pool_st *_memcached_client_pool;
  mongoc_client_pool_t *_mongodb_client_pool;
  ClientPool<ReviewStorageServiceClient> *_review_client_pool; // Added
  int _extra_latency_ms;
  int _ParseExtraLatency();
};

UserReviewHandler::UserReviewHandler(
    memcached_pool_st *memcached_client_pool,
    mongoc_client_pool_t *mongodb_client_pool,
    ClientPool<ReviewStorageServiceClient> *review_client_pool) {
  _memcached_client_pool = memcached_client_pool;
  _mongodb_client_pool = mongodb_client_pool;
  _review_client_pool = review_client_pool; // Initialized
  _extra_latency_ms = _ParseExtraLatency();
}

int UserReviewHandler::_ParseExtraLatency() {
  const char* extra_latency_env = std::getenv("EXTRA_LATENCY");
  if (extra_latency_env == nullptr) {
    return 0;
  }
  
  std::string latency_str(extra_latency_env);
  
  // Remove "ms" suffix if present
  if (latency_str.length() >= 2 && 
      latency_str.substr(latency_str.length() - 2) == "ms") {
    latency_str = latency_str.substr(0, latency_str.length() - 2);
  }
  
  try {
    int latency_ms = std::stoi(latency_str);
    if (latency_ms < 0) {
      LOG(warning) << "EXTRA_LATENCY cannot be negative, setting to 0";
      return 0;
    }
    LOG(info) << "EXTRA_LATENCY set to " << latency_ms << "ms";
    return latency_ms;
  } catch (const std::exception& e) {
    LOG(warning) << "Invalid EXTRA_LATENCY value: " << extra_latency_env 
                 << ", setting to 0";
    return 0;
  }
}

void UserReviewHandler::UploadUserReview(
    int64_t req_id, int64_t user_id, int64_t review_id,
    int64_t timestamp, const std::map<std::string, std::string> & carrier) {

  // Apply extra latency if configured
  if (_extra_latency_ms > 0) {
    LOG(debug) << "Adding extra latency of " << _extra_latency_ms 
               << "ms for request " << req_id;
    std::this_thread::sleep_for(std::chrono::milliseconds(_extra_latency_ms));
  }

  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "UploadUserReview",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  mongoc_client_t *mongodb_client = mongoc_client_pool_pop(
      _mongodb_client_pool);
  if (!mongodb_client) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to pop a client from MongoDB pool";
    throw se;
  }

  auto collection = mongoc_client_get_collection(
      mongodb_client, "user-review", "user-review");
  if (!collection) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to create collection user-review from DB user-review";
    mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
    throw se;
  }

  bson_t *query = bson_new();
  BSON_APPEND_INT64(query, "user_id", user_id);
  auto find_span = opentracing::Tracer::Global()->StartSpan(
      "MongoFindUser", {opentracing::ChildOf(&span->context())});
  mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
      collection, query, nullptr, nullptr);
  const bson_t *doc;
  bool found = mongoc_cursor_next(cursor, &doc);
  find_span->Finish(); // moved finish here
  if (!found) {
    bson_t *new_doc = BCON_NEW(
        "user_id", BCON_INT64(user_id),
        "reviews",
        "[", "{", "review_id", BCON_INT64(review_id),
        "timestamp", BCON_INT64(timestamp), "}", "]"
    );
    bson_error_t error;
    auto insert_span = opentracing::Tracer::Global()->StartSpan(
        "MongoInsert", {opentracing::ChildOf(&span->context())});
    bool plotinsert = mongoc_collection_insert_one(
        collection, new_doc, nullptr, nullptr, &error);
    insert_span->Finish();
    if (!plotinsert) {
      LOG(error) << "Failed to insert user review of user " << user_id
                 << " to MongoDB: " << error.message;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = error.message;
      bson_destroy(new_doc);
      bson_destroy(query);
      mongoc_cursor_destroy(cursor);
      mongoc_collection_destroy(collection);
      mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
      throw se;
    }
    bson_destroy(new_doc);
  } else {
    bson_t *update = BCON_NEW(
        "$push", "{",
        "reviews", "{",
        "$each", "[", "{",
        "review_id", BCON_INT64(review_id),
        "timestamp", BCON_INT64(timestamp),
        "}", "]",
        "$position", BCON_INT32(0),
        "}",
        "}"
    );
    bson_error_t error;
    bson_t reply;
    auto update_span = opentracing::Tracer::Global()->StartSpan(
        "MongoUpdate", {opentracing::ChildOf(&span->context())});
    bool plotupdate = mongoc_collection_find_and_modify(
        collection, query, nullptr, update, nullptr, false, false,
        true, &reply, &error);
    update_span->Finish();
    if (!plotupdate) {
      LOG(error) << "Failed to update user-review for user " << user_id
                 << " to MongoDB: " << error.message;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = error.message;
      bson_destroy(update);
      bson_destroy(query);
      bson_destroy(&reply);
      mongoc_cursor_destroy(cursor);
      mongoc_collection_destroy(collection);
      mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
      throw se;
    }
    bson_destroy(update);
    bson_destroy(&reply);
  }
  bson_destroy(query);
  mongoc_cursor_destroy(cursor);
  mongoc_collection_destroy(collection);
  mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);

  auto redis_client_wrapper = _memcached_client_pool->pop();
  if (!redis_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_REDIS_ERROR;
    se.message = "Cannot connected to Redis server";
    throw se;
  }
  auto redis_client = redis_client_wrapper->GetClient();
  auto redis_span = opentracing::Tracer::Global()->StartSpan(
      "RedisUpdate", {opentracing::ChildOf(&span->context())});
  std::vector<std::string> options;
  std::map<std::string, std::string> value = {{
      std::to_string(timestamp), std::to_string(review_id)}};
  redis_client->zadd(std::to_string(user_id), options, value);
  redis_client->sync_commit();

  _memcached_client_pool->push(redis_client_wrapper);
  redis_span->Finish();
  span->Finish();
}

void UserReviewHandler::ReadUserReviews(
    std::vector<Review> & _return, int64_t req_id,
    int64_t user_id, int32_t start, int32_t stop,
    const std::map<std::string, std::string> & carrier) {

  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "ReadUserReviews",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  if (stop <= start || start < 0) {
    return;
  }

  auto redis_client_wrapper = _memcached_client_pool->pop();
  if (!redis_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_REDIS_ERROR;
    se.message = "Cannot connected to Redis server";
    throw se;
  }
  auto redis_client = redis_client_wrapper->GetClient();
  auto redis_span = opentracing::Tracer::Global()->StartSpan(
      "RedisFind", {opentracing::ChildOf(&span->context())});
  auto review_ids_future = redis_client->zrevrange(
      std::to_string(user_id), start, stop - 1);
  redis_client->commit();
  _memcached_client_pool->push(redis_client_wrapper);
  redis_span->Finish();

  cpp_redis::reply review_ids_reply;
  try {
    review_ids_reply = review_ids_future.get();
  } catch (...) {
    LOG(error) << "Failed to read review_ids from user-review-redis";
    throw;
  }
  
  std::vector<int64_t> review_ids;
  auto review_ids_reply_array = review_ids_reply.as_array();
  for (auto &review_id_reply : review_ids_reply_array) {
    review_ids.emplace_back(std::stoul(review_id_reply.as_string()));
  }

  int mongo_start = start + review_ids.size();
  std::multimap<std::string, std::string> redis_update_map;
  if (mongo_start < stop) {
    // Instead find review_ids from mongodb
    mongoc_client_t *mongodb_client = mongoc_client_pool_pop(
        _mongodb_client_pool);
    if (!mongodb_client) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = "Failed to pop a client from MongoDB pool";
      throw se;
    }
    auto collection = mongoc_client_get_collection(
        mongodb_client, "user-review", "user-review");
    if (!collection) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = "Failed to create collection user-review from MongoDB";
      mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
      throw se;
    }

    bson_t *query = BCON_NEW("user_id", BCON_INT64(user_id));
    bson_t *opts = BCON_NEW(
        "projection", "{",
        "reviews", "{",
        "$slice", "[",
        BCON_INT32(0), BCON_INT32(stop),
        "]", "}", "}");
    auto find_span = opentracing::Tracer::Global()->StartSpan(
        "MongoFindUserReviews", {opentracing::ChildOf(&span->context())});
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        collection, query, opts, nullptr);
    find_span->Finish();
    const bson_t *doc;
    bool found = mongoc_cursor_next(cursor, &doc);
    if (found) {
      bson_iter_t iter;
      if (bson_iter_init(&iter, doc) && bson_iter_find_descendant(&iter, "reviews", &iter)) {
          if (BSON_ITER_HOLDS_ARRAY(&iter)) {
            bson_iter_t array_iter;
            if (bson_iter_init(&array_iter, doc) && bson_iter_find_descendant(&array_iter, "reviews", &array_iter)) {
              int idx = 0;
              while (bson_iter_next(&array_iter)) {
                bson_iter_t review_id_child;
                bson_iter_t timestamp_child;
                
                if (BSON_ITER_HOLDS_DOCUMENT(&array_iter)) {
                    const bson_t *review_doc;
                    bson_iter_document(&array_iter, &review_doc);

                    bson_iter_init(&review_id_child, review_doc);
                    bson_iter_init(&timestamp_child, review_doc);

                    if (bson_iter_find(&review_id_child, "review_id") && BSON_ITER_HOLDS_INT64(&review_id_child)
                        && bson_iter_find(&timestamp_child, "timestamp") && BSON_ITER_HOLDS_INT64(&timestamp_child)) {
                        auto curr_review_id = bson_iter_int64(&review_id_child);
                        auto curr_timestamp = bson_iter_int64(&timestamp_child);
                        if (idx >= start) {
                            review_ids.emplace_back(curr_review_id);
                        }
                        redis_update_map.insert(
                            {std::to_string(curr_timestamp), std::to_string(curr_review_id)});
                    }
                }
                idx++;
              }
            }
          }
      }
    }
    
    bson_destroy(opts);
    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
  }

  std::future<std::vector<Review>> review_future = std::async(
      std::launch::async, [&]() {
        auto review_client_wrapper = _review_client_pool->Pop();
        if (!review_client_wrapper) {
          ServiceException se;
          se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
          se.message = "Failed to connected to review-storage-service";
          throw se;
        }
        std::vector<Review> _return_reviews;
        auto review_client = review_client_wrapper->GetClient();
        try {
          review_client->ReadReviews(
              _return_reviews, req_id, review_ids, writer_text_map);
        } catch (...) {
          _review_client_pool->Push(review_client_wrapper);
          LOG(error) << "Failed to read review from review-storage-service";
          throw;
        }
        _review_client_pool->Push(review_client_wrapper);
        return _return_reviews;
      });

  std::future<cpp_redis::reply> zadd_reply_future;
  if (!redis_update_map.empty()) {
    // Update Redis
    redis_client_wrapper = _memcached_client_pool->pop();
    if (!redis_client_wrapper) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_REDIS_ERROR;
      se.message = "Cannot connected to Redis server";
      throw se;
    }
    redis_client = redis_client_wrapper->GetClient();
    auto redis_update_span = opentracing::Tracer::Global()->StartSpan(
        "RedisUpdate", {opentracing::ChildOf(&span->context())});
    redis_client->del(std::vector<std::string>{std::to_string(user_id)});
    std::vector<std::string> options;
    zadd_reply_future = redis_client->zadd(
        std::to_string(user_id), options, redis_update_map);
    redis_client->commit();
    _memcached_client_pool->push(redis_client_wrapper);
    redis_update_span->Finish();
  }

  try {
    _return = review_future.get();
  } catch (...) {
    LOG(error) << "Failed to get review from review-storage-service";
    if (!redis_update_map.empty()) {
      try {
        zadd_reply_future.get();
      } catch (...) {
        LOG(error) << "Failed to Update Redis Server";
      }
    }
    throw;
  }

  if (!redis_update_map.empty()) {
    try {
      zadd_reply_future.get();
    } catch (...) {
      LOG(error) << "Failed to Update Redis Server";
      throw;
    }
  }

  span->Finish();
}

} // namespace media_service
