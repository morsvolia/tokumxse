// visible_id_tracker.h

/**
*    Copyright (C) 2014 MongoDB Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*    As a special exception, the copyright holders give permission to link the
*    code of portions of this program with the OpenSSL library under certain
*    conditions as described in each individual source file and distribute
*    linked combinations including the program with the OpenSSL library. You
*    must comply with the GNU Affero General Public License in all respects for
*    all of the code used other than as permitted herein. If you modify file(s)
*    with this exception, you may extend this exception to your version of the
*    file(s), but you are not obligated to do so. If you do not wish to do so,
*    delete this exception statement from your version. If you delete this
*    exception statement from all source files in the program, then also delete
*    it in the license file.
*/

#pragma once

#include <set>

#include <boost/thread/mutex.hpp>

#include "mongo/db/operation_context.h"
#include "mongo/db/record_id.h"
#include "mongo/db/storage/kv/dictionary/kv_record_store.h"
#include "mongo/db/storage/kv/dictionary/kv_recovery_unit.h"
#include "mongo/db/storage/recovery_unit.h"

namespace mongo {

    class VisibleIdTracker {
    public:
        virtual ~VisibleIdTracker() {}

        virtual bool canReadId(const RecordId &id) const = 0;

        virtual void addUncommittedId(OperationContext *opCtx, const RecordId &id) = 0;

        virtual RecordId lowestInvisible() const = 0;

        virtual void setRecoveryUnitRestriction(KVRecoveryUnit *ru) const = 0;

        virtual void setIteratorRestriction(KVRecoveryUnit *ru, KVRecordStore::KVRecordIterator *iter) const = 0;
    };

    class NoopIdTracker : public VisibleIdTracker {
    public:
        bool canReadId(const RecordId &) const { return true; }
        void addUncommittedId(OperationContext *, const RecordId &) {}
        RecordId lowestInvisible() const { return RecordId::max(); }
        void setRecoveryUnitRestriction(KVRecoveryUnit *ru) const {}
        void setIteratorRestriction(KVRecoveryUnit *, KVRecordStore::KVRecordIterator *) const {}
    };

    class CappedIdTracker : public VisibleIdTracker {
    protected:
        mutable boost::mutex _mutex;
        std::set<RecordId> _uncommittedIds;
        RecordId _highest;

    public:
        CappedIdTracker(int64_t nextIdNum)
            : _highest(nextIdNum - 1)
        {}

        virtual ~CappedIdTracker() {}

        virtual bool canReadId(const RecordId &id) const {
            return id < lowestInvisible();
        }

        virtual void addUncommittedId(OperationContext *opCtx, const RecordId &id) {
            opCtx->recoveryUnit()->registerChange(new UncommittedIdChange(this, id));

            boost::mutex::scoped_lock lk(_mutex);
            _uncommittedIds.insert(id);
            if (id > _highest) {
                _highest = id;
            }
        }

        virtual RecordId lowestInvisible() const {
            boost::mutex::scoped_lock lk(_mutex);
            return (_uncommittedIds.empty()
                    ? RecordId(_highest.repr() + 1)
                    : *_uncommittedIds.begin());
        }

        virtual void setRecoveryUnitRestriction(KVRecoveryUnit *ru) const {}

        virtual void setIteratorRestriction(KVRecoveryUnit *ru, KVRecordStore::KVRecordIterator *iter) const {
            iter->setIdTracker(this);
        }

    protected:
        class UncommittedIdChange : public RecoveryUnit::Change {
            CappedIdTracker *_tracker;
            RecordId _id;

        public:
            UncommittedIdChange(CappedIdTracker *tracker, RecordId id)
                : _tracker(tracker),
                  _id(id)
            {}

            virtual void commit() {
                _tracker->markIdVisible(_id);
            }
            virtual void rollback() {
                _tracker->markIdVisible(_id);
            }
        };

        void markIdVisible(const RecordId &id) {
            boost::mutex::scoped_lock lk(_mutex);
            _uncommittedIds.erase(id);
        }

        friend class UncommittedIdChange;
    };

    class OplogIdTracker : public CappedIdTracker {
    public:
        OplogIdTracker(int64_t nextIdNum)
            : CappedIdTracker(nextIdNum)
        {}

        void setRecoveryUnitRestriction(KVRecoveryUnit *ru) const {
            if (!ru->hasSnapshot() || ru->getLowestInvisible().isNull()) {
                ru->setLowestInvisible(lowestInvisible());
            }
        }

        void setIteratorRestriction(KVRecoveryUnit *ru, KVRecordStore::KVRecordIterator *iter) const {
            CappedIdTracker::setIteratorRestriction(ru, iter);
            invariant(!ru->getLowestInvisible().isNull());
            iter->setLowestInvisible(ru->getLowestInvisible());
        }
    };

}
