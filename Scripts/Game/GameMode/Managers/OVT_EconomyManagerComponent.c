class OVT_EconomyManagerComponentClass: OVT_ComponentClass
{
};

class OVT_ShopInventoryItem : ScriptAndConfig
{
	[Attribute("Item Prefab", UIWidgets.ResourceNamePicker)]
	ResourceName prefab;
	
	[Attribute("50")]
	int cost;
	
	[Attribute("1")]
	int demandMultiplier;
}

class OVT_ShopInventoryConfig : ScriptAndConfig
{
	[Attribute("1", UIWidgets.ComboBox, "Shop type", "", ParamEnumArray.FromEnum(OVT_ShopType) )]
	OVT_ShopType type;
	
	[Attribute("", UIWidgets.Object)]
	ref array<ref OVT_ShopInventoryItem> m_aInventoryItems;
}

class OVT_EconomyManagerComponent: OVT_Component
{
	[Attribute("", UIWidgets.Object)]
	ref array<ref OVT_ShopInventoryConfig> m_aShopConfigs;
	
	[Attribute("", UIWidgets.Object)]
	ref array<ref OVT_ShopInventoryItem> m_aGunDealerItems;
			
	protected ref array<RplId> m_aAllShops;
	protected ref array<RplId> m_aAllPorts;
	protected ref array<RplId> m_aGunDealers;
	
	protected OVT_TownManagerComponent m_Towns;
	protected OVT_PlayerManagerComponent m_Players;
	
	protected ref map<ResourceName,EntityID> m_mSpawnedItems;
	
	const int ECONOMY_UPDATE_FREQUENCY = 60000;
	
	protected ref array<ref ResourceName> m_aResources;
	protected ref map<ref ResourceName,int> m_aResourceIndex;
	
	protected ref map<int, ref array<RplId>> m_mTownShops;
	
	protected ref map<int, ref OVT_ShopInventoryItem> m_mInventoryItems;
	
	
	
	//Streamed to clients..
	ref map<int, int> m_mItemCosts;		
	int m_iResistanceMoney = 0;
	float m_fResistanceTax = 0;
	
	//Events
	ref ScriptInvoker m_OnPlayerMoneyChanged = new ref ScriptInvoker();
	ref ScriptInvoker m_OnResistanceMoneyChanged = new ref ScriptInvoker();
		
	static OVT_EconomyManagerComponent s_Instance;	
	static OVT_EconomyManagerComponent GetInstance()
	{
		if (!s_Instance)
		{
			BaseGameMode pGameMode = GetGame().GetGameMode();
			if (pGameMode)
				s_Instance = OVT_EconomyManagerComponent.Cast(pGameMode.FindComponent(OVT_EconomyManagerComponent));
		}

		return s_Instance;
	}
	
	void OVT_EconomyManagerComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		m_aAllShops = new array<RplId>;	
		m_aAllPorts = new array<RplId>;
		m_mItemCosts = new map<int, int>;
		m_aGunDealers = new array<RplId>;
		m_mSpawnedItems = new map<ResourceName,EntityID>;
		m_mTownShops = new map<int, ref array<RplId>>;
		m_mInventoryItems = new map<int, ref OVT_ShopInventoryItem>;
		m_aResourceIndex = new map<ref ResourceName,int>;
	}
	
	void CheckUpdate()
	{
		PlayerManager mgr = GetGame().GetPlayerManager();		
		if(mgr.GetPlayerCount() == 0)
		{
			return;
		}
		
		TimeContainer time = m_Time.GetTime();		
		
		//Every 6 hrs get paid
		if((time.m_iHours == 0 
			|| time.m_iHours == 6 
			|| time.m_iHours == 12 
			|| time.m_iHours == 18)
			 && 
			time.m_iMinutes == 0)
		{
			CalculateIncome();
			UpdateShops();
		}
		
		//Every morning at 7am replenish stock
		if(time.m_iHours == 7 &&
			time.m_iMinutes == 0)
		{
			ReplenishStock();
		}
		
		//Every midnight calculate rents
		if(time.m_iHours == 0 &&
			time.m_iMinutes == 0)
		{
			UpdateRents();
		}
	}
	
	protected void UpdateRents()
	{
		OVT_RealEstateManagerComponent realEstate = OVT_Global.GetRealEstate();
		for(int i = 0; i < realEstate.m_mRenters.Count(); i++)
		{
			RplId id = realEstate.m_mRenters.GetKey(i);
			string playerId = realEstate.m_mRenters[id];
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
			if(!rpl) continue;
			IEntity entity = rpl.GetEntity();
			int cost = realEstate.GetRentPrice(entity);
			if(realEstate.IsOwner(playerId, entity.GetID()))
			{
				AddPlayerMoneyPersistentId(playerId, cost);
			}else{			
				if(!PlayerHasMoney(playerId, cost))
				{
					realEstate.SetRenter(-1, entity);
				}else{
					TakePlayerMoneyPersistentId(playerId, cost);
				}
			}
		}
	}
	
	protected void ReplenishStock()
	{
		//Shops restocking		
		foreach(OVT_TownData town : m_Towns.m_Towns)
		{
			int townID = OVT_Global.GetTowns().GetTownID(town);
			if(!m_mTownShops.Contains(townID)) continue;			
			
			//split shops into types
			map<int, ref array<RplId>> typeShops = GetShopTypes(townID);
			
			array<int> types = new array<int>;			
			for(int i=0; i<typeShops.Count(); i++)
			{
				types.Insert(typeShops.GetKey(i));
			}			
			
			foreach(RplId shopId : m_aAllShops)
			{
				OVT_ShopComponent shop = GetShopByRplId(shopId);
				foreach(int id : shop.m_aInventoryItemIds)
				{
					int max = GetTownMaxStock(townID, id);
					max = Math.Round(max / typeShops[shop.m_ShopType].Count());
					int stock = shop.GetStock(id);
					int half = Math.Round(stock * 0.5);
					if(stock < half)
					{
						shop.AddToInventory(id, half - stock);
					}
				}
			}
		}
	}
	
	protected void UpdateShops()
	{
		//NPCs Buying stock
		foreach(OVT_TownData town : m_Towns.m_Towns)
		{
			int townID = OVT_Global.GetTowns().GetTownID(town);
			if(!m_mTownShops.Contains(townID)) continue;
			int maxToBuy = (town.population * m_Config.m_fNPCBuyRate) * (town.stability / 100);
			
			int numToBuy = s_AIRandomGenerator.RandInt(1,maxToBuy);
			
			//split shops into types
			map<int, ref array<RplId>> typeShops = GetShopTypes(townID);
			
			array<int> types = new array<int>;			
			for(int i=0; i<typeShops.Count(); i++)
			{
				types.Insert(typeShops.GetKey(i));
			}			
			
			//buy stuff
			for(int i=0; i<numToBuy; i++)
			{
				//pick a random shop type
				int typeIndex = s_AIRandomGenerator.RandInt(0,types.Count()-1);
				//pick a random shop within that type
				int shopIndex = s_AIRandomGenerator.RandInt(0,typeShops[types[typeIndex]].Count()-1);
				OVT_ShopComponent shop = GetShopByRplId(typeShops[types[typeIndex]][shopIndex]);
				if(!shop) continue;
				//pick a random inventory item
				int itemIndex = s_AIRandomGenerator.RandInt(0,shop.m_aInventoryItemIds.Count()-1);
				int id = shop.m_aInventoryItemIds[itemIndex];
				OVT_ShopInventoryItem item = GetInventoryItem(id);
				int qty = s_AIRandomGenerator.RandInt(1,item.demandMultiplier);
				int stock = shop.GetStock(id);
				if(stock < qty) qty = stock;
				if(qty > 0)
				{
					shop.HandleNPCSale(id, qty);
				}
			}			
		}
	}
	
	protected map<int, ref array<RplId>> GetShopTypes(int townId)
	{
		map<int, ref array<RplId>> typeShops = new map<int, ref array<RplId>>;
		for(int i=0; i<m_mTownShops[townId].Count(); i++)
		{				
			RplId shopId = m_mTownShops[townId][i];
			OVT_ShopComponent shop = GetShopByRplId(shopId);
			if(!shop || shop.m_ShopType == -1) continue;
			if(!typeShops.Contains(shop.m_ShopType))
			{
				typeShops[shop.m_ShopType] = new array<RplId>;
			}
			typeShops[shop.m_ShopType].Insert(shopId);
		}
		return typeShops;
	}
	
	OVT_ShopComponent GetShopByRplId(RplId shopId)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(shopId));
		if(!rpl) return null;
		IEntity entity = rpl.GetEntity();
		return OVT_ShopComponent.Cast(entity.FindComponent(OVT_ShopComponent));
	}
	
	protected void CalculateIncome()
	{
		//Support donations
		int income = GetDonationIncome();
		income += GetTaxIncome();
		
		if(income == 0) return;
		
		int taxed = Math.Round(income * m_fResistanceTax);
		income -= taxed;
		AddResistanceMoney(taxed);
		
		PlayerManager mgr = GetGame().GetPlayerManager();
		int count = mgr.GetPlayerCount();
		if(count == 0)
		{
			AddResistanceMoney(income);
			return;
		}
		//Distribute remaining to all players online
		int incomePerPlayer = Math.Round(income / count);
		
		array<int> players = new array<int>;
		mgr.GetPlayers(players);
		foreach(int playerId : players)
		{			
			AddPlayerMoney(playerId, incomePerPlayer);
		}
		
		
		
	}
	
	int GetDonationIncome()
	{
		int income = 0;
		foreach(OVT_TownData town : m_Towns.m_Towns)
		{
			income += m_Config.m_Difficulty.donationIncome * town.support;
		}
		return income;
	}
	
	int GetTaxIncome()
	{
		int income = 0;
		
		foreach(OVT_TownData town : m_Towns.m_Towns)
		{
			if(town.IsOccupyingFaction()) continue;
			income += (int)Math.Round(m_Config.m_Difficulty.taxIncome * town.population * (town.stability / 100));
		}
		
		return income;
	}
	
	void SetPrice(int id, int cost)
	{
		m_mItemCosts[id] = cost;
	}
	
	int GetSellPrice(int id, vector pos = "0 0 0")
	{
		if(m_mItemCosts.Count()-1 < id) return 0;
		
		int price = GetMinPrice(id);
		if(pos[0] != 0)
		{
			OVT_TownData town = OVT_Global.GetTowns().GetNearestTown(pos);
			if(town)
			{
				int townID = OVT_Global.GetTowns().GetTownID(town);
				int stock_level = GetTownStock(townID, id);
				int max_stock = GetTownMaxStock(townID, id);
				float distance_to_port = DistanceToNearestPort(pos);
				price = Math.Round(price + ((1 - (stock_level / max_stock)) * (price * 0.1)) + (price * distance_to_port * 0.0001));
				if(price < 0) price = 1;
			}
		}
		
		return price;
	}
	
	int GetBuyPrice(int id, vector pos = "0 0 0")
	{
		int price = GetSellPrice(id, pos);
		int buy = Math.Round(price + (price * m_Config.m_fShopProfitMargin));
		if(buy == price) buy += 1;
		return buy;
	}
	
	int GetTownStock(int townId, int id)
	{
		if(!m_mTownShops.Contains(townId)) return 0;
		int stock = 0;
		foreach(RplId shopId : m_mTownShops[townId])
		{
			OVT_ShopComponent shop = GetShopByRplId(shopId);
			stock += shop.GetStock(id);
		}
		return stock;
	}
	
	int GetTownMaxStock(int townId, int id)
	{
		OVT_ShopInventoryItem item = GetInventoryItem(id);
		if(!item) return 100;
		OVT_TownData town = m_Towns.GetTown(townId);
		if(!town) return 100;
		return Math.Round(1 + (town.population * m_Config.m_fNPCBuyRate * item.demandMultiplier * ((float)town.stability / 100)));
	}	
	
	int GetMinPrice(int id)
	{
		return m_mItemCosts[id];
	}
	
	int GetPriceByResource(ResourceName res, vector pos = "0 0 0")
	{
		int id = GetInventoryId(res);
		return GetSellPrice(id, pos);
	}
	
	bool IsSoldAtShop(ResourceName res, OVT_ShopType shopType)
	{
		OVT_ShopInventoryConfig config = GetShopConfig(shopType);
		foreach(OVT_ShopInventoryItem item : config.m_aInventoryItems)
		{
			if(item.prefab == res)
			{
				return true;
			}
		}
		return false;
	}
	
	RplId GetNearestPort(vector pos)
	{
		RplId nearestPort;
		float nearest = -1;
		foreach(RplId id : m_aAllPorts)
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
			float distance = vector.Distance(pos, rpl.GetEntity().GetOrigin());
			if(nearest == -1 || distance < nearest){
				nearest = distance;
				nearestPort = id;
			}
		}
		return nearestPort;
	}
	
	float DistanceToNearestPort(vector pos)
	{
		float nearest = -1;
		foreach(RplId id : m_aAllPorts)
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
			float distance = vector.Distance(pos, rpl.GetEntity().GetOrigin());
			if(nearest == -1 || distance < nearest){
				nearest = distance;
			}
		}
		return nearest;
	}
	
	array<RplId> GetAllShops()
	{
		return m_aAllShops;
	}
	
	array<RplId> GetAllPorts()
	{
		return m_aAllPorts;
	}
	
	array<RplId> GetGunDealers()
	{
		return m_aGunDealers;
	}
	
	array<RplId> GetAllShopsInTown(OVT_TownData town)
	{
		int range = m_Towns.m_iCityRange;
		if(town.size == 2) range = m_Towns.m_iTownRange;
		if(town.size == 1) range = m_Towns.m_iVillageRange;
		array<RplId> shops = new array<RplId>;
		foreach(RplId id : m_aAllShops)
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
			float dist = vector.Distance(town.location, rpl.GetEntity().GetOrigin());
			if(dist <= range) shops.Insert(id);
		}
		return shops;
	}
	
	OVT_ShopInventoryConfig GetShopConfig(OVT_ShopType shopType)
	{		
		foreach(OVT_ShopInventoryConfig config : m_aShopConfigs)
		{
			if(config.type == shopType) return config;
		}
		return new OVT_ShopInventoryConfig();
	}
	
	int GetPlayerMoney(string playerId)
	{
		OVT_PlayerData player = m_Players.GetPlayer(playerId);
		if(!player) return 0;
		return player.money;
	}
	
	int GetLocalPlayerMoney()
	{
		string playerId = OVT_Global.GetPlayers().GetPersistentIDFromPlayerID(SCR_PlayerController.GetLocalPlayerId());
		
		return GetPlayerMoney(playerId);
	}
	
	int LocalPlayerHasMoney(int amount)
	{
		string playerId = OVT_Global.GetPlayers().GetPersistentIDFromPlayerID(SCR_PlayerController.GetLocalPlayerId());		
		return PlayerHasMoney(playerId, amount);
	}
	
	bool PlayerHasMoney(string playerId, int amount)
	{
		OVT_PlayerData player = m_Players.GetPlayer(playerId);
		if(!player) return false;
		return player.money >= amount;
	}
	
	void AddPlayerMoney(int playerId, int amount)
	{
		if(Replication.IsServer())
		{
			DoAddPlayerMoney(playerId, amount);
			return;
		}
		OVT_Global.GetServer().AddPlayerMoney(playerId, amount);		
	}
	
	void DoAddPlayerMoney(int playerId, int amount)
	{
		string persId = OVT_Global.GetPlayers().GetPersistentIDFromPlayerID(playerId);
		
		OVT_PlayerData player = m_Players.GetPlayer(persId);
		if(!player) return;
		
		player.money = player.money + amount;
		OVT_Global.GetEconomy().StreamPlayerMoney(playerId);
		m_OnPlayerMoneyChanged.Invoke(persId, player.money);
	}
	
	void AddResistanceMoney(int amount)
	{
		if(Replication.IsServer())
		{
			DoAddResistanceMoney(amount);
			return;
		}
		OVT_Global.GetServer().AddResistanceMoney(amount);		
	}
	
	void DoAddResistanceMoney(int amount)
	{		
		RpcDo_SetResistanceMoney(m_iResistanceMoney + amount);
		StreamResistanceMoney();
	}
	
	void TakeResistanceMoney(int amount)
	{
		if(Replication.IsServer())
		{
			DoTakeResistanceMoney(amount);
			return;
		}
		OVT_Global.GetServer().TakeResistanceMoney(amount);		
	}
	
	void DoTakeResistanceMoney(int amount)
	{		
		RpcDo_SetResistanceMoney(m_iResistanceMoney - amount);
		StreamResistanceMoney();
	}
	
	void SetResistanceTax(float amount)
	{
		if(Replication.IsServer())
		{
			DoSetResistanceTax(amount);
			return;
		}
		OVT_Global.GetServer().SetResistanceTax(amount);		
	}
	
	void DoSetResistanceTax(float amount)
	{		
		RpcDo_SetResistanceTax(amount);
		StreamResistanceTax();
	}
	
	void TakeLocalPlayerMoney(int amount)
	{
		int id = SCR_PlayerController.GetLocalPlayerId();
		TakePlayerMoney(id, amount);
	}
	
	void TakePlayerMoney(int playerId, int amount)
	{
		if(Replication.IsServer())
		{
			DoTakePlayerMoney(playerId, amount);
			return;
		}
		OVT_Global.GetServer().TakePlayerMoney(playerId, amount);	
	}
	
	void TakePlayerMoneyPersistentId(string persId, int amount)
	{
		OVT_PlayerData player = m_Players.GetPlayer(persId);
		if(!player) return;		
		
		player.money = player.money - amount;
		if(player.money < 0) player.money = 0;
		
		int playerId = OVT_Global.GetPlayers().GetPlayerIDFromPersistentID(persId);
		if(playerId > -1){
			StreamPlayerMoney(playerId);	
		}
		m_OnPlayerMoneyChanged.Invoke(persId, player.money);
	}
	
	void AddPlayerMoneyPersistentId(string persId, int amount)
	{
		OVT_PlayerData player = m_Players.GetPlayer(persId);
		if(!player) return;
		
		player.money = player.money + amount;
		
		int playerId = OVT_Global.GetPlayers().GetPlayerIDFromPersistentID(persId);
		if(playerId > -1){
			StreamPlayerMoney(playerId);	
		}
		m_OnPlayerMoneyChanged.Invoke(persId, player.money);
	}
	
	void DoTakePlayerMoney(int playerId, int amount)
	{
		string persId = OVT_Global.GetPlayers().GetPersistentIDFromPlayerID(playerId);
		OVT_PlayerData player = m_Players.GetPlayer(persId);
		if(!player) return;
		
		player.money = player.money - amount;
		if(player.money < 0) player.money = 0;
		StreamPlayerMoney(playerId);	
		m_OnPlayerMoneyChanged.Invoke(persId, player.money);
	}
	
	bool ResistanceHasMoney(int amount)
	{
		return m_iResistanceMoney >= amount;
	}
	
	int GetResistanceMoney()
	{
		return m_iResistanceMoney;
	}
	
	void RegisterGunDealer(EntityID id)
	{
		IEntity entity = GetGame().GetWorld().FindEntityByID(id);
		RplComponent rpl = RplComponent.Cast(entity.FindComponent(RplComponent));
		if(rpl)
			m_aGunDealers.Insert(rpl.Id());
	}
	
	void Init(IEntity owner)
	{			
		m_Towns = OVT_Global.GetTowns();
		m_Players = OVT_Global.GetPlayers();
		
		BuildResourceDatabase();
		GetGame().GetCallqueue().CallLater(InitializePorts, 0);
		
		if(!Replication.IsServer()) return;		
		InitializeShops();		
		
		GetGame().GetCallqueue().CallLater(CheckUpdate, ECONOMY_UPDATE_FREQUENCY / m_Config.m_iTimeMultiplier, true, GetOwner());
		
	}
	
	void RegisterResource(ResourceName res)
	{
		if(!m_aResources.Contains(res))
		{
			m_aResources.Insert(res);
			int id = m_aResources.Count()-1;
			m_aResourceIndex[res] = id;
		}
	}
	
	void BuildResourceDatabase()
	{
		m_aResources = new array<ref ResourceName>;
		
		foreach(OVT_ShopInventoryConfig config : m_aShopConfigs)
		{
			foreach(OVT_ShopInventoryItem item : config.m_aInventoryItems)
			{
				ResourceName res = item.prefab;
				if(!m_aResources.Contains(res))
				{
					m_aResources.Insert(res);
					int id = m_aResources.Count()-1;
					m_mInventoryItems[id] = item;	
					m_aResourceIndex[res] = id;
				}
			}
		}
		
		foreach(OVT_ShopInventoryItem item : m_aGunDealerItems)
		{
			ResourceName res = item.prefab;
			if(!m_aResources.Contains(res))
			{
				m_aResources.Insert(res);
				int id = m_aResources.Count()-1;
				m_mInventoryItems[id] = item;	
				m_aResourceIndex[res] = id;
			}
		}
		
		FactionManager factionMgr = GetGame().GetFactionManager();
		array<Faction> factions = new array<Faction>;
		factionMgr.GetFactionsList(factions);
		foreach(Faction faction : factions)
		{
			OVT_Faction fac = OVT_Faction.Cast(faction);
			array<SCR_EntityCatalogEntry> items = new array<SCR_EntityCatalogEntry>;
			fac.GetAllInventoryItems(items);
			foreach(SCR_EntityCatalogEntry item : items) 
			{
				ResourceName res = item.GetPrefab();
				if(res == "") continue;
				if(!m_aResources.Contains(res))
				{
					m_aResources.Insert(res);
					int id = m_aResources.Count()-1;
					m_aResourceIndex[res] = id;
				}
			}
		}
	}
	
	OVT_ShopInventoryItem GetInventoryItem(int id)
	{
		return m_mInventoryItems[id];
	}
	
	
			
	void PostGameStart()
	{		
		GetGame().GetCallqueue().CallLater(InitShopInventory, 0);				
	}
	
	protected void InitializePorts()
	{
		#ifdef OVERTHROW_DEBUG
		Print("Finding ports");
		#endif
		
		GetGame().GetWorld().QueryEntitiesBySphere("0 0 0", 99999999, CheckPortInit, FilterPortEntities, EQueryEntitiesFlags.DYNAMIC);
	}
	
	protected void InitializeShops()
	{
		#ifdef OVERTHROW_DEBUG
		Print("Finding shops");
		#endif
		
		GetGame().GetWorld().QueryEntitiesBySphere("0 0 0", 99999999, CheckShopInit, FilterShopEntities, EQueryEntitiesFlags.STATIC);
	}
		
	int GetInventoryId(ResourceName res)
	{		
		return m_aResourceIndex[res];
	}
	
	ResourceName GetResource(int id)
	{
		return m_aResources[id];
	}
	
	protected void InitShopInventory()
	{
		foreach(RplId shopId : m_aAllShops)
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(shopId));
			if(!rpl) continue;
			IEntity entity = rpl.GetEntity();
			OVT_ShopComponent shop = OVT_ShopComponent.Cast(entity.FindComponent(OVT_ShopComponent));
			OVT_TownData town = shop.GetTown();
			
			int townID = OVT_Global.GetTowns().GetTownID(town);
		
			OVT_ShopInventoryConfig config = GetShopConfig(shop.m_ShopType);
			foreach(OVT_ShopInventoryItem item : config.m_aInventoryItems)
			{
				int id = GetInventoryId(item.prefab);				
				SetPrice(id, item.cost);
				
				int max = GetTownMaxStock(townID, id);
				
				int num = Math.Round(s_AIRandomGenerator.RandFloatXY(1,max));
				
				shop.AddToInventory(id, num);
				
				shop.m_aInventoryItems.Insert(item);
				shop.m_aInventoryItemIds.Insert(id);
			}
		}
	}
	
	protected bool CheckShopInit(IEntity entity)
	{	
		#ifdef OVERTHROW_DEBUG
		Print("Found Shop");
		#endif
		
		RplComponent rpl = RplComponent.Cast(entity.FindComponent(RplComponent));
		if(rpl)
		{
			RplId id = rpl.Id();
			m_aAllShops.Insert(id);
		
			OVT_TownData town = m_Towns.GetNearestTown(entity.GetOrigin());
			int townID = OVT_Global.GetTowns().GetTownID(town);
			
			if(!m_mTownShops.Contains(townID))
			{				
				m_mTownShops[townID] = new ref array<RplId>;	
			}
			
			m_mTownShops[townID].Insert(id);
			
			OVT_ShopComponent shop = GetShopByRplId(id);
			shop.m_iTownId = townID;
		}
		
		return true;
	}
	
	protected bool FilterShopEntities(IEntity entity)
	{	
		if(entity.ClassName() == "SCR_DestructibleBuildingEntity")
		{
			OVT_ShopComponent shop = OVT_ShopComponent.Cast(entity.FindComponent(OVT_ShopComponent));
			if(shop) return true;
		}
		return false;
	}
	
	protected bool CheckPortInit(IEntity entity)
	{	
		//#ifdef OVERTHROW_DEBUG
		Print("Found Port");
		//#endif
		
		RplComponent rpl = RplComponent.Cast(entity.FindComponent(RplComponent));
		if(rpl)
		{
			RplId id = rpl.Id();
			m_aAllPorts.Insert(id);
		}
		
		return true;
	}
	
	protected bool FilterPortEntities(IEntity entity)
	{	

		OVT_PortControllerComponent port = OVT_PortControllerComponent.Cast(entity.FindComponent(OVT_PortControllerComponent));
		if(port) return true;

		return false;
	}
	
	//RPC Methods
	
	
	override bool RplSave(ScriptBitWriter writer)
	{		
		//Send JIP price list
		writer.WriteInt(m_mItemCosts.Count()); 
		for(int i=0; i<m_mItemCosts.Count(); i++)
		{			
			writer.WriteInt(m_mItemCosts.GetKey(i));
			writer.WriteInt(m_mItemCosts.GetElement(i));
		}
		
		writer.WriteInt(m_iResistanceMoney);
		writer.WriteFloat(m_fResistanceTax);
		
		//Send JIP Shops
		writer.WriteInt(m_aAllShops.Count()); 
		for(int i=0; i<m_aAllShops.Count(); i++)
		{
			writer.WriteRplId(m_aAllShops[i]);
		}
		writer.WriteInt(m_mTownShops.Count()); 
		for(int i=0; i<m_mTownShops.Count(); i++)
		{			
			int key = m_mTownShops.GetKey(i);
			writer.WriteInt(key);
			writer.WriteInt(m_mTownShops[key].Count());
			for(int t=0; t<m_mTownShops[key].Count(); t++)
			{	
				writer.WriteRplId(m_mTownShops[key][t]);
			}			
		}
		
		//Send JIP Gun Dealers
		writer.WriteInt(m_aGunDealers.Count()); 
		for(int i=0; i<m_aGunDealers.Count(); i++)
		{
			writer.WriteRplId(m_aGunDealers[i]);
		}
		
		return true;
	}
	
	override bool RplLoad(ScriptBitReader reader)
	{
		
		//Recieve JIP price list
		int length, keylength, price;
		string playerId;
		RplId id;
		int key;
		
		if (!reader.ReadInt(length)) return false;
		for(int i=0; i<length; i++)
		{
			if (!reader.ReadInt(key)) return false;
			if (!reader.ReadInt(price)) return false;
			m_mItemCosts[key] = price;
		}
		
		if (!reader.ReadInt(m_iResistanceMoney)) return false;
		if (!reader.ReadFloat(m_fResistanceTax)) return false;
		
		//Recieve JIP shops		
		if (!reader.ReadInt(length)) return false;
		for(int i=0; i<length; i++)
		{			
			if (!reader.ReadRplId(id)) return false;
			m_aAllShops.Insert(id);
		}
		
		if (!reader.ReadInt(length)) return false;
		for(int i=0; i<length; i++)
		{	
			if (!reader.ReadInt(key)) return false;
			m_mTownShops[key] = new array<RplId>;
			if (!reader.ReadInt(keylength)) return false;
			for(int t=0; t<keylength; t++)
			{
				if (!reader.ReadRplId(id)) return false;
				m_mTownShops[key].Insert(id);
			}
		}
		
		//Recieve JIP gun dealers		
		if (!reader.ReadInt(length)) return false;
		for(int i=0; i<length; i++)
		{			
			if (!reader.ReadRplId(id)) return false;
			m_aGunDealers.Insert(id);
		}
		
		return true;
	}
	
	
	void StreamPlayerMoney(int playerId)
	{
		string persId = OVT_Global.GetPlayers().GetPersistentIDFromPlayerID(playerId);
		OVT_PlayerData player = m_Players.GetPlayer(persId);
		if(!player) return;
		
		Rpc(RpcDo_SetPlayerMoney, playerId, player.money);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetPlayerMoney(int playerId, int amount)
	{
		string persId = OVT_Global.GetPlayers().GetPersistentIDFromPlayerID(playerId);
		OVT_PlayerData player = m_Players.GetPlayer(persId);
		if(!player) return;
		player.money = amount;
		m_OnPlayerMoneyChanged.Invoke(persId, player.money);
	}
	
	protected void StreamResistanceMoney()
	{
		Rpc(RpcDo_SetResistanceMoney, m_iResistanceMoney);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetResistanceMoney(int amount)
	{
		m_iResistanceMoney = amount;
		m_OnResistanceMoneyChanged.Invoke(m_iResistanceMoney);
	}
	
	protected void StreamResistanceTax()
	{
		Rpc(RpcDo_SetResistanceTax, m_fResistanceTax);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetResistanceTax(float amount)
	{
		m_fResistanceTax = amount;
	}
	
	void ~OVT_EconomyManagerComponent()
	{
		GetGame().GetCallqueue().Remove(CheckUpdate);	
		
		if(m_aAllShops)
		{
			m_aAllShops.Clear();
			m_aAllShops = null;
		}
		if(m_aGunDealers)
		{
			m_aGunDealers.Clear();
			m_aGunDealers = null;
		}
		if(m_mSpawnedItems)
		{
			m_mSpawnedItems.Clear();
			m_mSpawnedItems = null;
		}
	}
}