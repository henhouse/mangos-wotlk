/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "AuctionHouseBot.h"
#include "Globals/ObjectMgr.h"
#include "Log.h"
#include "Policies/Singleton.h"
#include "ProgressBar.h"
#include "SystemConfig.h"
#include "World/World.h"

// Format is YYYYMMDDRR where RR is the change in the conf file
// for that day.
#define AUCTIONHOUSEBOT_CONF_VERSION    2020010101

INSTANTIATE_SINGLETON_1(AuctionHouseBot);

AuctionHouseBot::AuctionHouseBot() : m_configFileName(_AUCTIONHOUSEBOT_CONFIG), m_houseAction(-1) {
}


AuctionHouseBot::~AuctionHouseBot() {
}

void AuctionHouseBot::Initialize() {
    if (!m_ahBotCfg.SetSource(m_configFileName)) {
        sLog.outString("AHBot is disabled. Unable to open configuration file(%s).", m_configFileName.c_str());
        return;
    }
    sLog.outString("AHBot using configuration file %s", m_configFileName.c_str());

    sLog.outString("AHBot will %ssell items at the Auction House", m_ahBotCfg.GetBoolDefault("AuctionHouseBot.Sell.Enabled", false) ? "" : "NOT ");
    sLog.outString("AHBot will %sbuy items from the Auction House", m_ahBotCfg.GetBoolDefault("AuctionHouseBot.Buy.Enabled", false) ? "" : "NOT ");

    if (m_ahBotCfg.GetBoolDefault("AuctionHouseBot.Sell.Enabled", false)) {
        // creature loot
        parseLootConfig("AuctionHouseBot.Loot.Creature.Normal", m_creatureLootNormalConfig);
        parseLootConfig("AuctionHouseBot.Loot.Creature.Elite", m_creatureLootEliteConfig);
        parseLootConfig("AuctionHouseBot.Loot.Creature.RareElite", m_creatureLootRareEliteConfig);
        parseLootConfig("AuctionHouseBot.Loot.Creature.WorldBoss", m_creatureLootWorldBossConfig);
        parseLootConfig("AuctionHouseBot.Loot.Creature.Rare", m_creatureLootRareConfig);
        fillUintVectorFromQuery("SELECT entry FROM creature_template WHERE rank = 0 AND entry IN (SELECT entry FROM creature_loot_template)", m_creatureLootNormalTemplates);
        fillUintVectorFromQuery("SELECT entry FROM creature_template WHERE rank = 1 AND entry IN (SELECT entry FROM creature_loot_template)", m_creatureLootEliteTemplates);
        fillUintVectorFromQuery("SELECT entry FROM creature_template WHERE rank = 2 AND entry IN (SELECT entry FROM creature_loot_template)", m_creatureLootRareEliteTemplates);
        fillUintVectorFromQuery("SELECT entry FROM creature_template WHERE rank = 3 AND entry IN (SELECT entry FROM creature_loot_template)", m_creatureLootWorldBossTemplates);
        fillUintVectorFromQuery("SELECT entry FROM creature_template WHERE rank = 4 AND entry IN (SELECT entry FROM creature_loot_template)", m_creatureLootRareTemplates);

        // disenchant loot
        parseLootConfig("AuctionHouseBot.Loot.Disenchant", m_disenchantLootConfig);
        fillUintVectorFromQuery("SELECT DISTINCT entry FROM disenchant_loot_template", m_disenchantLootTemplates);

        // fishing loot
        parseLootConfig("AuctionHouseBot.Loot.Fishing", m_fishingLootConfig);
        fillUintVectorFromQuery("SELECT DISTINCT entry FROM fishing_loot_template", m_fishingLootTemplates);

        // gameobject loot
        parseLootConfig("AuctionHouseBot.Loot.Gameobject", m_gameobjectLootConfig);
        fillUintVectorFromQuery("SELECT DISTINCT entry FROM gameobject_loot_template WHERE entry IN (SELECT data1 FROM gameobject_template WHERE entry IN (SELECT id FROM gameobject WHERE state = 1 AND spawntimesecsmax > 0))", m_gameobjectLootTemplates);

        // skinning loot
        parseLootConfig("AuctionHouseBot.Loot.Skinning", m_skinningLootConfig);
        fillUintVectorFromQuery("SELECT DISTINCT entry FROM skinning_loot_template", m_skinningLootTemplates);

        // item price
        parseItemPriceConfig("AuctionHouseBot.Price.Poor", m_itemPrice[ITEM_QUALITY_POOR]);
        parseItemPriceConfig("AuctionHouseBot.Price.Normal", m_itemPrice[ITEM_QUALITY_NORMAL]);
        parseItemPriceConfig("AuctionHouseBot.Price.Uncommon", m_itemPrice[ITEM_QUALITY_UNCOMMON]);
        parseItemPriceConfig("AuctionHouseBot.Price.Rare", m_itemPrice[ITEM_QUALITY_RARE]);
        parseItemPriceConfig("AuctionHouseBot.Price.Epic", m_itemPrice[ITEM_QUALITY_EPIC]);
        parseItemPriceConfig("AuctionHouseBot.Price.Legendary", m_itemPrice[ITEM_QUALITY_LEGENDARY]);
        parseItemPriceConfig("AuctionHouseBot.Price.Artifact", m_itemPrice[ITEM_QUALITY_ARTIFACT]);
        // item price variance
        setMinMaxConfig("AuctionHouseBot.Price.Variance", m_itemPriceVariance, 0, 100, 10);
        // auction min/max bid
        setMinMaxConfig("AuctionHouseBot.Bid.Min", m_auctionBidMin, 0, 100, 60);
        setMinMaxConfig("AuctionHouseBot.Bid.Max", m_auctionBidMax, 0, 100, 90);
        if (m_auctionBidMin > m_auctionBidMax) {
            sLog.outError("AHBot error: AuctionHouseBot.Bid.Min must be less or equal to AuctionHouseBot.Bid.Max. Setting Bid.Min equal to Bid.Max.");
            m_auctionBidMin = m_auctionBidMax;
        }
        // auction min/max time
        setMinMaxConfig("AuctionHouseBot.Time.Min", m_auctionTimeMin, 1, 72, 2);
        setMinMaxConfig("AuctionHouseBot.Time.Max", m_auctionTimeMax, 1, 72, 24);
        if (m_auctionTimeMin > m_auctionTimeMax) {
            sLog.outError("AHBot error: AuctionHouseBot.Time.Min must be less or equal to AuctionHouseBot.Time.Max. Setting Time.Min equal to Time.Max.");
            m_auctionTimeMin = m_auctionTimeMax;
        }

        // multiplier for items sold by vendors
        setMinMaxConfig("AuctionHouseBot.Vendor.Multiplier", m_vendorMultiplier, 0, 8, 4);

        // probability that AHBot will visit the AH for buying items
        setMinMaxConfig("AuctionHouseBot.Buy.Check", m_buyCheckChance, 0, 100, 20);

        // vendor items
        std::vector<uint32> tmpVector;
        fillUintVectorFromQuery("SELECT DISTINCT item FROM npc_vendor", tmpVector);
        std::copy(tmpVector.begin(), tmpVector.end(), std::inserter(m_vendorItems, m_vendorItems.end()));
    }
}

void AuctionHouseBot::parseLootConfig(char const* fieldname, std::vector<int32>& lootConfig) {
    std::stringstream includeStream(m_ahBotCfg.GetStringDefault(fieldname));
    std::string temp;
    lootConfig.clear();
    while (getline(includeStream, temp, ','))
        lootConfig.push_back(atoi(temp.c_str()));
    if (lootConfig.size() > 4)
        sLog.outError("AHBot error: Too many values specified for field %s (%lu), 4 values required. Additional values ignored.", fieldname, lootConfig.size());
    else if (lootConfig.size() < 4) {
        sLog.outError("AHBot error: Too few values specified for field %s (%lu), 4 values required. Setting 0 for remaining values.", fieldname, lootConfig.size());
        while (lootConfig.size() < 4)
            lootConfig.push_back(0);
    }
    for (uint32 index = 1; index < 4; ++index) {
        if (lootConfig[index] < 0) {
            sLog.outError("AHBot error: %s value (%d) for field %s should not be a negative number, setting value to 0.", (index == 1 ? "First" : (index == 2 ? "Second" : "Third")), lootConfig[1], fieldname);
            lootConfig[index] = 0;
        }
    }
    if (lootConfig[0] > lootConfig[1]) {
        sLog.outError("AHBot error: First value (%d) must be less than or equal to second value (%d) for field %s. Setting first value to second value.", lootConfig[0], lootConfig[1], fieldname);
        lootConfig[0] = lootConfig[1];
    }
    if (lootConfig[2] > lootConfig[3]) {
        sLog.outError("AHBot error: Third value (%d) must be less than or equal to fourth value (%d) for field %s. Setting third value to fourth value.", lootConfig[2], lootConfig[3], fieldname);
        lootConfig[2] = lootConfig[3];
    }
}

void AuctionHouseBot::fillUintVectorFromQuery(char const* query, std::vector<uint32>& lootTemplates) {
    lootTemplates.clear();
    if (QueryResult* result = WorldDatabase.PQuery("%s", query)) {
        BarGoLink bar(result->GetRowCount());
        do {
            bar.step();
            Field* fields = result->Fetch();
            uint32 entry = fields[0].GetUInt32();
            if (!entry)
                continue;
            lootTemplates.push_back(fields[0].GetUInt32());
        } while (result->NextRow());
        delete result;
    }
}

void AuctionHouseBot::setMinMaxConfig(const char* config, uint32& field, uint32 minValue, uint32 maxValue, uint32 defaultValue) {
    field = m_ahBotCfg.GetIntDefault(config, defaultValue);
    if (field < minValue) {
        sLog.outError("AHBot error: %s must be between %u and %u. Setting value to %u.", config, minValue, maxValue, defaultValue);
        field = defaultValue;
    } else if (field > maxValue) {
        sLog.outError("AHBot error: %s must be between %u and %u. Setting value to %u.", config, minValue, maxValue, defaultValue);
        field = defaultValue;
    }
}

void AuctionHouseBot::parseItemPriceConfig(char const* fieldname, std::vector<uint32>& itemPrices) {
    std::stringstream includeStream(m_ahBotCfg.GetStringDefault(fieldname));
    std::string temp;
    for (uint32 index = 0; getline(includeStream, temp, ','); ++index) {
        if (index < itemPrices.size())
            itemPrices[index] = atoi(temp.c_str());
    }
}

void AuctionHouseBot::addLootToItemMap(LootStore* store, std::vector<int32>& lootConfig, std::vector<uint32>& lootTemplates, std::unordered_map<uint32, uint32>& itemMap) {
    if (lootConfig[1] <= 0 || lootConfig[3] <= 0)
        return;
    int32 maxTemplates = lootConfig[0] < 0 ? urand(0, lootConfig[1] + 1 - lootConfig[0]) + lootConfig[0] : urand(lootConfig[0], lootConfig[1] + 1);
    if (maxTemplates <= 0)
        return;
    for (uint32 templateCounter = 0; templateCounter < maxTemplates; ++templateCounter) {
        uint32 lootTemplate = urand(0, lootTemplates.size());
        LootTemplate const* lootTable = store->GetLootFor(lootTemplates[lootTemplate]);
        if (!lootTable)
            continue;
        std::unique_ptr<Loot> loot = std::unique_ptr<Loot>(new Loot(LOOT_DEBUG));
        for (uint32 repeat = urand(lootConfig[2], lootConfig[3]); repeat > 0; --repeat)
            lootTable->Process(*loot, nullptr, *store, store->IsRatesAllowed());

        LootItem* lootItem;
        for (uint32 slot = 0; (lootItem = loot->GetLootItemInSlot(slot)); ++slot)
            itemMap[lootItem->itemId] += lootItem->count;
    }
}

bool AuctionHouseBot::ReloadAllConfig() {
    Initialize();
    return true;
}

void AuctionHouseBot::PrepareStatusInfos(AuctionHouseBotStatusInfo& statusInfo) const {
    for (uint32 i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i) {
        statusInfo[i].ItemsCount = 0;

        for (unsigned int& j : statusInfo[i].QualityInfo)
            j = 0;

        AuctionHouseObject::AuctionEntryMapBounds bounds = sAuctionMgr.GetAuctionsMap(AuctionHouseType(i))->GetAuctionsBounds();
        for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr) {
            AuctionEntry* entry = itr->second;
            if (Item* item = sAuctionMgr.GetAItem(entry->itemGuidLow)) {
                ItemPrototype const* prototype = item->GetProto();
                if (!entry->owner) { // count only ahbot auctions
                    if (prototype->Quality < MAX_ITEM_QUALITY)
                        ++statusInfo[i].QualityInfo[prototype->Quality];

                    ++statusInfo[i].ItemsCount;
                }
            }
        }
    }
}

void AuctionHouseBot::Rebuild(bool all) {
    sLog.outError("AHBot: Rebuilding auction house items");
    for (uint32 i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i) {
        AuctionHouseObject::AuctionEntryMapBounds bounds = sAuctionMgr.GetAuctionsMap(AuctionHouseType(i))->GetAuctionsBounds();
        for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr) {
            AuctionEntry* entry = itr->second;
            if (!entry->owner) { // ahbot auction
                if (all || entry->bid == 0) // expire auction if no bid or forced
                    entry->expireTime = sWorld.GetGameTime();
            }
        }
    }
    // refill auction house with items, simulating typical max amount of items available after some time
    uint32 updateCounter = ((m_auctionTimeMax - m_auctionTimeMin) / 4 + m_auctionTimeMin) * 90;
    for (uint32 i = 0; i < updateCounter; ++i) {
        if (m_houseAction >= MAX_AUCTION_HOUSE_TYPE - 1)
            m_houseAction = -1; // this prevents AHBot from buying items when refilling
        Update();
    }
}

void AuctionHouseBot::Update() {
    if (++m_houseAction >= MAX_AUCTION_HOUSE_TYPE * 2)
        m_houseAction = 0;

    AuctionHouseType houseType = AuctionHouseType(m_houseAction % MAX_AUCTION_HOUSE_TYPE);
    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(houseType);
    if (m_houseAction < MAX_AUCTION_HOUSE_TYPE) {
        if (!m_ahBotCfg.GetBoolDefault("AuctionHouseBot.Sell.Enabled", false))
            return; // selling disabled
        // Sell items
        std::unordered_map<uint32, uint32> itemMap;

        addLootToItemMap(&LootTemplates_Creature, m_creatureLootNormalConfig, m_creatureLootNormalTemplates, itemMap);       // normal creature loot
        addLootToItemMap(&LootTemplates_Creature, m_creatureLootEliteConfig, m_creatureLootEliteTemplates, itemMap);         // elite creature loot
        addLootToItemMap(&LootTemplates_Creature, m_creatureLootRareEliteConfig, m_creatureLootRareEliteTemplates, itemMap); // rare elite creature loot
        addLootToItemMap(&LootTemplates_Creature, m_creatureLootWorldBossConfig, m_creatureLootWorldBossTemplates, itemMap); // world boss creature loot
        addLootToItemMap(&LootTemplates_Creature, m_creatureLootRareConfig, m_creatureLootRareTemplates, itemMap);           // rare creature loot

        addLootToItemMap(&LootTemplates_Disenchant, m_disenchantLootConfig, m_disenchantLootTemplates, itemMap);             // disenchant loot
        addLootToItemMap(&LootTemplates_Fishing, m_fishingLootConfig, m_fishingLootTemplates, itemMap);                      // fishing loot
        addLootToItemMap(&LootTemplates_Gameobject, m_gameobjectLootConfig, m_gameobjectLootTemplates, itemMap);             // gameobject loot
        addLootToItemMap(&LootTemplates_Skinning, m_skinningLootConfig, m_skinningLootTemplates, itemMap);                   // skinning loot

        for (auto itemEntry : itemMap) {
            ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(itemEntry.first);
            if (!prototype || prototype->GetMaxStackSize() == 0)
                continue; // really shouldn't happen, but better safe than sorry
            if (prototype->Bonding == BIND_WHEN_PICKED_UP || prototype->Bonding == BIND_QUEST_ITEM)
                continue; // nor BoP and quest items
            if (prototype->Flags & ITEM_FLAG_HAS_LOOT)
                continue; // no items containing loot
            if (m_itemPrice[prototype->Quality][prototype->Class] == 0)
                continue; // item class is filtered out

            for (uint32 stackCounter = 0; stackCounter < itemEntry.second; stackCounter += prototype->GetMaxStackSize()) {
                Item* item = Item::CreateItem(itemEntry.first, itemEntry.second - stackCounter > prototype->GetMaxStackSize() ? prototype->GetMaxStackSize() : itemEntry.second - stackCounter);
                if (!item)
                    continue;
                uint32 buyoutPrice = prototype->SellPrice;
                if (buyoutPrice == 0) // fall back to buy price if no sell price (needed for enchanting mats)
                    buyoutPrice = prototype->BuyPrice / 4;
                if (buyoutPrice == 0)
                    continue;
                buyoutPrice *= item->GetCount() * m_itemPrice[prototype->Quality][prototype->Class];
                buyoutPrice += ((int32) urand(0, m_itemPriceVariance * 2 + 1) - (int32) m_itemPriceVariance) * (int32) (buyoutPrice / 100);
                uint32 bidPrice = buyoutPrice * (urand(m_auctionBidMin, m_auctionBidMax + 1)) / 100;
                auctionHouse->AddAuction(sAuctionHouseStore.LookupEntry(houseType == AUCTION_HOUSE_ALLIANCE ? 1 : (houseType == AUCTION_HOUSE_HORDE ? 6 : 7)), item, urand(m_auctionTimeMin, m_auctionTimeMax + 1) * HOUR, bidPrice, buyoutPrice);
            }
        }
    } else {
        if (!m_ahBotCfg.GetBoolDefault("AuctionHouseBot.Buy.Enabled", false))
            return; // buying disabled
        if (urand(0, 100) >= m_buyCheckChance)
            return; // AHBot should not buy any items this time
        // Buy items
        AuctionHouseObject::AuctionEntryMapBounds bounds = auctionHouse->GetAuctionsBounds();
        for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr) {
            AuctionEntry* auction = itr->second;
            if (auction->owner == 0 && auction->bid == 0)
                continue; // ignore bidding/buying auctions that were created by server and not bidded on by player
            Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow);
            if (!item)
                continue; // shouldn't happen
            ItemPrototype const* prototype = item->GetProto();
            if (!prototype)
                continue; // shouldn't happen
            uint32 buyoutPrice = prototype->SellPrice;
            if (buyoutPrice == 0) // fall back to buy price if no sell price (needed for enchanting mats)
                buyoutPrice = prototype->BuyPrice / 4;
            if (buyoutPrice == 0)
                continue;
            // multiply buyoutPrice with count and quality multiplier
            // if item is sold by a vendor and the vendor multiplier is set, then multiply by this instead
            buyoutPrice *= item->GetCount() * (m_vendorMultiplier == 0 || m_vendorItems.find(prototype->ItemId) == m_vendorItems.end() ? m_itemPrice[prototype->Quality][prototype->Class] : m_vendorMultiplier);
            buyoutPrice += ((int32) urand(0, m_itemPriceVariance * 2 + 1) - (int32) m_itemPriceVariance) * (int32) (buyoutPrice / 100);
            uint32 buyItemCheck = urand(0, buyoutPrice);
            uint32 bidPrice = auction->bid + auction->GetAuctionOutBid();
            if (auction->startbid > bidPrice)
                bidPrice = auction->startbid;
            if (buyItemCheck > auction->buyout)
                auction->UpdateBid(auction->buyout);
            else if (buyItemCheck > bidPrice)
                auction->UpdateBid(bidPrice);
        }
    }
}
