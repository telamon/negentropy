#include <iostream>
#include <cstdlib>

#include <sstream>
#include <memory>
#include <set>

#include <hoytech/error.h>
#include <hoytech/hex.h>

#include "negentropy.h"
#include "negentropy/storage/BTreeLMDB.h"
#include "negentropy/storage/BTreeMem.h"
#include "negentropy/storage/btree/debug.h"
#include "negentropy/storage/Vector.h"





int main() {
    system("mkdir -p testdb/");
    system("rm -f testdb/*");

    auto env = lmdb::env::create();
    env.set_max_dbs(64);
    env.open("testdb/", 0);


    lmdb::dbi btreeDbi;

    {
        auto txn = lmdb::txn::begin(env);
        btreeDbi = negentropy::storage::BTreeLMDB::setupDB(txn, "test-data");
        txn.commit();
    }

    negentropy::storage::Vector vec;


    auto packId = [](uint64_t n){
        auto o = std::string(32, '\0');
        memcpy((char*)o.data(), (char*)&n, sizeof(n));
        return o;
    };

    auto unpackId = [](std::string_view n){
        if (n.size() != 32) throw hoytech::error("too short to unpack");
        return *(uint64_t*)n.data();
    };


    {
        auto txn = lmdb::txn::begin(env);
        negentropy::storage::BTreeLMDB btree(txn, btreeDbi, 300);

        auto add = [&](uint64_t timestamp){
            negentropy::Item item(timestamp, packId(timestamp));
            btree.insertItem(item);
            vec.insertItem(item);
        };

        for (size_t i = 1000; i < 2000; i += 2) add(i);

        btree.flush();

        txn.commit();
    }

    vec.seal();




    {
        auto txn = lmdb::txn::begin(env, 0, MDB_RDONLY);
        negentropy::storage::BTreeLMDB btree(txn, btreeDbi, 300);
        //negentropy::storage::btree::dump(btree);
        negentropy::storage::btree::verify(btree, true);
    }



    // Identical

    {
        auto txn = lmdb::txn::begin(env, 0, MDB_RDONLY);
        negentropy::storage::BTreeLMDB btree(txn, btreeDbi, 300);

        auto ne1 = Negentropy(vec);
        auto ne2 = Negentropy(btree);

        auto q = ne1.initiate();

        std::vector<std::string> responderHave, responderNeed;
        auto response = ne2.reconcile(q, responderHave, responderNeed);
        if (!response) throw hoytech::error("bad reconcile response");

        std::vector<std::string> have, need;
        auto q3 = ne1.reconcile(*response, have, need);
        if (q3 || have.size() || need.size() || responderHave.size() || responderNeed.size()) throw hoytech::error("bad reconcile 1");
    }


    // Make some modifications

    {
        auto txn = lmdb::txn::begin(env);
        negentropy::storage::BTreeLMDB btree(txn, btreeDbi, 300);

        btree.erase(1044, packId(1044));
        btree.erase(1838, packId(1838));

        btree.insert(1555, packId(1555));
        btree.insert(99999, packId(99999));

        btree.flush();
        txn.commit();
    }


    // Reconcile again

    {
        auto txn = lmdb::txn::begin(env, 0, MDB_RDONLY);
        negentropy::storage::BTreeLMDB btree(txn, btreeDbi, 300);

        auto ne1 = Negentropy(vec);
        auto ne2 = Negentropy(btree);

        std::vector<uint64_t> allHave, allNeed, allResponderHave, allResponderNeed;

        std::string msg = ne1.initiate();

        while (true) {
            std::vector<std::string> responderHave, responderNeed;
            auto response = ne2.reconcile(msg, responderHave, responderNeed);
            if (!response) throw hoytech::error("bad reconcile response");

            for (const auto &id : responderHave) allResponderHave.push_back(unpackId(id));
            for (const auto &id : responderNeed) allResponderNeed.push_back(unpackId(id));

            std::vector<std::string> have, need;
            auto newMsg = ne1.reconcile(*response, have, need);

            for (const auto &id : have) allHave.push_back(unpackId(id));
            for (const auto &id : need) allNeed.push_back(unpackId(id));

            if (!newMsg) break; // done
            msg = *newMsg;
        }

        std::sort(allHave.begin(), allHave.end());
        std::sort(allNeed.begin(), allNeed.end());
        std::sort(allResponderHave.begin(), allResponderHave.end());
        std::sort(allResponderNeed.begin(), allResponderNeed.end());

        if (allHave != std::vector<uint64_t>({ 1044, 1838 })) throw hoytech::error("bad allHave");
        if (allNeed != std::vector<uint64_t>({ 1555, 99999 })) throw hoytech::error("bad allNeed");
        if (allResponderHave != allNeed) throw hoytech::error("bad allResponderHave");
        if (allResponderNeed != allHave) throw hoytech::error("bad allResponderNeed");
    }


    std::cout << "OK" << std::endl;

    return 0;
}
