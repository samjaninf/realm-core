/*************************************************************************
 *
 * Copyright 2021 Realm, Inc.
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
 *
 **************************************************************************/

#ifndef REALM_SYNC_SUBSCRIPTIONS_HPP
#define REALM_SYNC_SUBSCRIPTIONS_HPP

#include <realm/db.hpp>
#include <realm/obj.hpp>
#include <realm/query.hpp>
#include <realm/timestamp.hpp>
#include <realm/util/checked_mutex.hpp>
#include <realm/util/future.hpp>
#include <realm/util/functional.hpp>
#include <realm/util/optional.hpp>
#include <realm/util/tagged_bool.hpp>

#include <list>
#include <set>
#include <string_view>

namespace realm::sync {

class MutableSubscriptionSet;
class SubscriptionSet;
class SubscriptionStore;

// A Subscription represents a single query that may be OR'd with other queries on the same object class to be
// send to the server in a QUERY or IDENT message.
class Subscription {
public:
    // The unique ID for this subscription.
    ObjectId id;

    // The timestamp of when this subscription was originally created.
    Timestamp created_at;

    // The timestamp of the last time this subscription was updated by calling update_query.
    Timestamp updated_at;

    // The name of the subscription that was set when it was created, or util::none if it was created without a name.
    util::Optional<std::string> name;

    // The name of the object class of the query for this subscription.
    std::string object_class_name;

    // A stringified version of the query associated with this subscription.
    std::string query_string;

    // Returns whether the 2 subscriptions passed have the same id.
    friend bool operator==(const Subscription& lhs, const Subscription& rhs)
    {
        return lhs.id == rhs.id;
    }

    Subscription() = default;
    Subscription(const SubscriptionStore* parent, Obj obj);
    Subscription(util::Optional<std::string> name, std::string object_class_name, std::string query_str);
};

// SubscriptionSets contain a set of unique queries by either name or Query object that will be constructed into a
// single QUERY or IDENT message to be sent to the server.
class SubscriptionSet {
public:
    /*
     * State diagram:
     *
     *                    ┌───────────┬─────────►Error──────────────────────────┐
     *                    │           │                                         │
     *                    │           │                                         ▼
     *   Uncommitted──►Pending──►Bootstrapping──►AwaitingMark──►Complete───►Superseded
     *                    │                            ▲
     *                    │                            │
     *                    └────────────────────────────┘
     *
     */
    enum class State {
        // This subscription set has not been persisted and has not been sent to the server. This state is only valid
        // for MutableSubscriptionSets
        Uncommitted = 0,
        // The subscription set has been persisted locally but has not been acknowledged by the server yet.
        Pending,
        // The server is currently sending the initial state that represents this subscription set to the client.
        Bootstrapping,
        // This subscription set is the active subscription set that is currently being synchronized with the server.
        Complete,
        // An error occurred while processing this subscription set on the server. Check error_str() for details.
        Error,
        // The server responded to a later subscription set to this one and this one has been trimmed from the
        // local storage of subscription sets.
        Superseded,
        // The last bootstrap message containing the initial state for this subscription set has been received. The
        // client is awaiting a mark message to mark this subscription as fully caught up to history.
        AwaitingMark,
    };

    static constexpr int64_t EmptyVersion = int64_t(-1);

    // Used in tests.
    inline friend std::ostream& operator<<(std::ostream& o, State state)
    {
        switch (state) {
            case State::Uncommitted:
                o << "Uncommitted";
                break;
            case State::Pending:
                o << "Pending";
                break;
            case State::Bootstrapping:
                o << "Bootstrapping";
                break;
            case State::AwaitingMark:
                o << "AwaitingMark";
                break;
            case State::Complete:
                o << "Complete";
                break;
            case State::Error:
                o << "Error";
                break;
            case State::Superseded:
                o << "Superseded";
                break;
        }
        return o;
    }

    using const_iterator = std::vector<Subscription>::const_iterator;
    using iterator = const_iterator; // Note: no mutable access provided through iterators.

    // This will make a copy of this subscription set with the next available version number and return it as
    // a mutable SubscriptionSet to be updated. The new SubscriptionSet's state will be Uncommitted. This
    // subscription set will be unchanged.
    MutableSubscriptionSet make_mutable_copy() const;

    // Returns a future that will resolve either with an error status if this subscription set encounters an
    // error, or resolves when the subscription set reaches at least that state. It's possible for a subscription
    // set to skip a state (i.e. go from Pending to Complete or Pending to Superseded), and the future value
    // will the the state it actually reached.
    util::Future<State> get_state_change_notification(State notify_when) const;
    void get_state_change_notification(
        State notify_when, util::UniqueFunction<void(util::Optional<State>, util::Optional<Status>)> callback) const;

    // The query version number used in the sync wire protocol to identify this subscription set to the server.
    int64_t version() const;

    // The database version that this subscription set was created at or -1 if Uncommitted.
    DB::version_type snapshot_version() const;

    // The current state of this subscription set
    State state() const;

    // The error string for this subscription set if any.
    StringData error_str() const;

    // Returns the number of subscriptions in the set.
    size_t size() const;

    // An iterator interface for finding/working with individual subscriptions.
    iterator begin() const;
    iterator end() const;

    const Subscription& at(size_t index) const;

    // Returns a pointer to the Subscription matching either the name or Query object, or nullptr if no such
    // subscription exists.
    const Subscription* find(StringData name) const;
    const Subscription* find(const Query& query) const;

    // Returns this query set as extended JSON in a form suitable for transmitting to the server.
    std::string to_ext_json() const;

    // Reloads the state of this SubscriptionSet so that it reflects the latest state from synchronizing with the
    // server. This will invalidate all iterators.
    void refresh();

protected:
    friend class SubscriptionStore;
    struct SupersededTag {};
    using MakingMutableCopy = util::TaggedBool<class MakingMutableCopyTag>;

    explicit SubscriptionSet(std::weak_ptr<SubscriptionStore> mgr, int64_t version, SupersededTag);
    explicit SubscriptionSet(std::weak_ptr<SubscriptionStore> mgr, const Transaction& tr, const Obj& obj,
                             MakingMutableCopy making_mutable_copy = false);

    void load_from_database(const Obj& obj);
    void import(SubscriptionSet&&);

    // Get a reference to the SubscriptionStore. It may briefly extend the lifetime of the store.
    std::shared_ptr<SubscriptionStore> get_flx_subscription_store() const;

    std::weak_ptr<SubscriptionStore> m_mgr;

    DB::version_type m_cur_version = 0;
    int64_t m_version = 0;
    State m_state = State::Uncommitted;
    std::string m_error_str;
    DB::version_type m_snapshot_version = -1;
    std::vector<Subscription> m_subs;
    ObjKey m_obj_key;
};

class MutableSubscriptionSet : public SubscriptionSet {
public:
    // Erases all subscriptions in the subscription set.
    void clear();

    // Inserts a new subscription into the set if one does not exist already - returns an iterator to the
    // subscription and a bool that is true if a new subscription was actually created. The SubscriptionSet
    // must be in the Uncommitted state to call this - otherwise this will throw.
    //
    // The Query portion of the subscription is mutable, however the name portion is immutable after the
    // subscription is inserted.
    //
    // If insert is called twice for the same name, the Query portion and updated_at timestamp for that named
    // subscription will be updated to match the new Query.
    std::pair<iterator, bool> insert_or_assign(std::string_view name, const Query& query);

    // Inserts a new subscription into the set if one does not exist already - returns an iterator to the
    // subscription and a bool that is true if a new subscription was actually created. The SubscriptionSet
    // must be in the Uncommitted state to call this - otherwise this will throw.
    //
    // If insert is called twice for the same query, then the updated_at timestamp for that subscription will
    // be updated.
    //
    // The inserted subscription will have an empty name - to update this Subscription's query, the caller
    // will have
    std::pair<iterator, bool> insert_or_assign(const Query& query);

    void import(SubscriptionSet&&);

    // Erases a subscription pointed to by an iterator. Returns the "next" iterator in the set - to provide
    // STL compatibility. The SubscriptionSet must be in the Uncommitted state to call this - otherwise
    // this will throw.
    iterator erase(iterator it);

    // Erases the subscription identified by the argument, if any. Returns true if anything was removed.
    bool erase(StringData name);
    bool erase(const Query& query);

    bool erase_by_class_name(StringData object_class_name);
    bool erase_by_id(ObjectId id);

    // This commits any changes to the subscription set and returns an this subscription set as an immutable view
    // from after the commit. This MutableSubscriptionSet object must not be used after calling commit().
    SubscriptionSet commit();

    // For testing and internal usage only.
    void set_state(State new_state);

protected:
    friend class SubscriptionStore;
    // Allow the MigrationStore access to insert_sub because it cannot use insert_or_assign due to having the query as
    // a string and not a Query object.
    friend class MigrationStore;

    MutableSubscriptionSet(std::weak_ptr<SubscriptionStore> mgr, TransactionRef tr, Obj obj);

    void insert_sub(const Subscription& sub);

private:
    // To refresh a MutableSubscriptionSet, you should call commit() and call refresh() on its return value.
    void refresh() = delete;

    std::pair<iterator, bool> insert_or_assign_impl(iterator it, util::Optional<std::string> name,
                                                    std::string object_class_name, std::string query_str);
    // Throws is m_tr is in the wrong state.
    void check_is_mutable() const;

    void insert_sub_impl(ObjectId id, Timestamp created_at, Timestamp updated_at, StringData name,
                         StringData object_class_name, StringData query_str);

    TransactionRef m_tr;
    Obj m_obj;
};

class SubscriptionStore;
using SubscriptionStoreRef = std::shared_ptr<SubscriptionStore>;

// A SubscriptionStore manages the FLX metadata tables, SubscriptionSets and Subscriptions.
class SubscriptionStore : public std::enable_shared_from_this<SubscriptionStore> {
    struct Private {};

public:
    static SubscriptionStoreRef create(DBRef db);

    explicit SubscriptionStore(Private, DBRef db);
    SubscriptionStore(const SubscriptionStore&) = delete;
    SubscriptionStore& operator=(const SubscriptionStore&) = delete;

    // Get the latest subscription created by calling update_latest(). Once bootstrapping is complete,
    // this and get_active() will return the same thing. If no SubscriptionSet has been set, then
    // this returns an empty SubscriptionSet that you can clone() in order to mutate.
    SubscriptionSet get_latest();

    // Gets the subscription set that has been acknowledged by the server as having finished bootstrapping.
    // If no subscriptions have reached the complete stage, this returns an empty subscription with version
    // zero.
    SubscriptionSet get_active();

    struct VersionInfo {
        int64_t latest;
        int64_t active;
        int64_t pending_mark;
    };
    // Returns the version number of the current active and latest subscription sets. This function guarantees
    // that the versions will be read from the same underlying transaction and will thus be consistent.
    VersionInfo get_version_info() const;

    // To be used internally by the sync client. This returns a read-only view of a subscription set by its
    // version ID. If there is no SubscriptionSet with that version ID, this throws KeyNotFound.
    SubscriptionSet get_by_version(int64_t version_id) REQUIRES(!m_pending_notifications_mutex);

    // Returns true if there have been commits to the DB since the given version
    bool would_refresh(DB::version_type version) const noexcept;

    using TableSet = std::set<std::string, std::less<>>;
    TableSet get_tables_for_latest(const Transaction& tr) const;

    struct PendingSubscription {
        int64_t query_version;
        DB::version_type snapshot_version;
    };

    util::Optional<PendingSubscription> get_next_pending_version(int64_t last_query_version) const;
    std::vector<SubscriptionSet> get_pending_subscriptions() REQUIRES(!m_pending_notifications_mutex);

    // Mark query_version as having received an error from the server. Will
    // throw an exception if the version is not in a state where an error is
    // expected (i.e. if it's already completed or superseded).
    //
    // This should only be called internally within the sync client.
    void set_error(int64_t query_version, std::string_view error_str);
    // Mark query_version as having begun bootstrapping. This should be called
    // inside the write transaction used to store the first set of changesets.
    // Has no effect if the version is already complete. Throws if the version
    // is superseded or errored.
    //
    // This should only be called internally within the sync client.
    void begin_bootstrap(const Transaction&, int64_t query_version);
    // Mark query_version as having completed bootstrapping. This should be
    // called inside the write transaction which removes the final pending changeset.
    // Has no effect if the version is already complete. Throws if the version
    // is superseded or errored.
    //
    // This should only be called internally within the sync client.
    void complete_bootstrap(const Transaction&, int64_t query_version);
    // Roll query_version back to the Pending state if it is currently Bootstrapping.
    // Has no effect if the bootstrap in progress is not the first boostrap for
    // this subscription set.
    //
    // This should only be called internally within the sync client.
    void cancel_bootstrap(const Transaction&, int64_t query_version);
    // Report that a download has completed, meaning that the active subscription
    // set should advance to the Completed state if it is currently in the
    // AwaitingMark state. Has no effect if it is in any other state.
    //
    // This should only be called internally within the sync client.
    void download_complete();

    // If there are any notifications registered, check if they have been completed
    // and fulfill them if so.
    void report_progress() REQUIRES(!m_pending_notifications_mutex);
    void report_progress(TransactionRef& tr) REQUIRES(!m_pending_notifications_mutex);

    // Get the query version which we most recently received a DOWNLOAD message
    // for (which may be distinct from both the latest and active versions).
    int64_t get_downloading_query_version(Transaction& rt) const;

    // Mark the currently active subscription set as being complete without going
    // through the normal bootstrapping flow. Used for client resets where we
    // copy the data for the subscription over from the fresh Realm.
    int64_t mark_active_as_complete(Transaction& wt) REQUIRES(!m_pending_notifications_mutex);

    // Notify all subscription state change notification handlers on this subscription store with the
    // provided Status - this does not change the state of any pending subscriptions.
    // Does not necessarily need to be called from the event loop thread.
    void notify_all_state_change_notifications(Status status) REQUIRES(!m_pending_notifications_mutex);

    // Reset SubscriptionStore and erase all current subscriptions and supersede any pending
    // subscriptions. Must be called from the event loop thread to prevent data race issues
    // with the subscription store.
    void reset(Transaction& wt) REQUIRES(!m_pending_notifications_mutex);

    // Recreate the active subscription set, marking any newer pending ones as
    // superseded. This is a no-op if there are no pending subscription sets.
    int64_t set_active_as_latest(Transaction& wt) REQUIRES(!m_pending_notifications_mutex);

private:
    using State = SubscriptionSet::State;
    using std::enable_shared_from_this<SubscriptionStore>::weak_from_this;
    DBRef m_db;

    struct NotificationRequest {
        NotificationRequest(int64_t version, util::Promise<SubscriptionSet::State> promise,
                            SubscriptionSet::State notify_when)
            : version(version)
            , promise(std::move(promise))
            , notify_when(notify_when)
        {
        }

        int64_t version;
        util::Promise<SubscriptionSet::State> promise;
        SubscriptionSet::State notify_when;
    };

    Obj get_active(const Transaction& tr);
    SubscriptionSet get_refreshed(ObjKey, int64_t flx_version, TransactionRef tr);
    MutableSubscriptionSet make_mutable_copy(const SubscriptionSet& set);

    // Ensure the subscriptions table is properly initialized. No-op if already initialized.
    void initialize_subscriptions_table(TransactionRef&& tr);
    // Clear the table and reinitialize it.
    void clear(Transaction& wt);
    void do_complete_bootstrap(const Transaction&, int64_t query_version, SubscriptionSet::State new_state);

    friend class MutableSubscriptionSet;
    friend class Subscription;
    friend class SubscriptionSet;

    TableKey m_sub_table;
    ColKey m_sub_id;
    ColKey m_sub_created_at;
    ColKey m_sub_updated_at;
    ColKey m_sub_name;
    ColKey m_sub_object_class_name;
    ColKey m_sub_query_str;

    TableKey m_sub_set_table;
    ColKey m_sub_set_version_num;
    ColKey m_sub_set_snapshot_version;
    ColKey m_sub_set_state;
    ColKey m_sub_set_error_str;
    ColKey m_sub_set_subscriptions;

    util::CheckedMutex m_pending_notifications_mutex;
    std::list<NotificationRequest> m_pending_notifications GUARDED_BY(m_pending_notifications_mutex);
};

} // namespace realm::sync

#endif // REALM_SYNC_SUBSCRIPTIONS_HPP
