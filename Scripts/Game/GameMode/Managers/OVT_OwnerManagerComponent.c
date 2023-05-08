class OVT_OwnerManagerComponentClass: OVT_ComponentClass
{
	
}

class OVT_OwnerManagerComponent: OVT_Component
{
	ref map<string, ref set<RplId>> m_mOwned;
	ref map<string, ref set<RplId>> m_mRented;
	ref map<ref RplId, string> m_mOwners;
	ref map<ref RplId, string> m_mRenters;
	
	void OVT_OwnerManagerComponent()
	{
		m_mOwned = new map<string, ref set<RplId>>;
		m_mOwners = new map<ref RplId, string>;
		m_mRented = new map<string, ref set<RplId>>;
		m_mRenters = new map<ref RplId, string>;
	}
	
	void SetOwner(int playerId, IEntity building)
	{
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		DoSetOwner(playerId, rpl.Id());		
		Rpc(RpcDo_SetOwner, playerId, rpl.Id());		
	}
	
	void SetOwnerPersistentId(string persId, IEntity building)
	{
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		int playerId = OVT_Global.GetPlayers().GetPlayerIDFromPersistentID(persId);
		DoSetOwnerPersistentId(persId, rpl.Id());		
		Rpc(RpcDo_SetOwner, playerId, rpl.Id());		
	}
	
	void SetRenter(int playerId, IEntity building)
	{
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		DoSetRenter(playerId, rpl.Id());		
		Rpc(RpcDo_SetRenter, playerId, rpl.Id());		
	}
	
	void SetRenterPersistentId(string persId, IEntity building)
	{
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		int playerId = OVT_Global.GetPlayers().GetPlayerIDFromPersistentID(persId);
		DoSetRenterPersistentId(persId, rpl.Id());		
		Rpc(RpcDo_SetRenter, playerId, rpl.Id());		
	}
	
	string GetOwnerID(IEntity building)
	{
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		if(!rpl) return "";
		if(!m_mOwners.Contains(rpl.Id())) return "";
		return m_mOwners[rpl.Id()];
	}
	
	string GetRenterID(IEntity building)
	{
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		if(!rpl) return "";
		if(!m_mRenters.Contains(rpl.Id())) return "";
		return m_mRenters[rpl.Id()];
	}
	
	bool IsOwner(string playerId, EntityID entityId)
	{
		if(!m_mOwned.Contains(playerId)) return false;
		IEntity building = GetGame().GetWorld().FindEntityByID(entityId);
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		set<RplId> owner = m_mOwned[playerId];
		return owner.Contains(rpl.Id());
	}
	
	bool IsRenter(string playerId, EntityID entityId)
	{
		if(!m_mRented.Contains(playerId)) return false;
		IEntity building = GetGame().GetWorld().FindEntityByID(entityId);
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		set<RplId> rented = m_mRented[playerId];
		return rented.Contains(rpl.Id());
	}
	
	bool IsOwned(EntityID entityId)
	{
		IEntity building = GetGame().GetWorld().FindEntityByID(entityId);
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		for(int i=0; i< m_mOwned.Count(); i++)
		{
			set<RplId> owner = m_mOwned.GetElement(i);
			if(owner.Contains(rpl.Id()))
			{
				return true;
			}
		}
		return false;
	}
	
	bool IsRented(EntityID entityId)
	{
		IEntity building = GetGame().GetWorld().FindEntityByID(entityId);
		RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
		for(int i=0; i< m_mRented.Count(); i++)
		{
			set<RplId> rented = m_mRented.GetElement(i);
			if(rented.Contains(rpl.Id()))
			{
				return true;
			}
		}
		return false;
	}
	
	set<EntityID> GetOwned(string playerId)
	{
		if(!m_mOwned.Contains(playerId)) return new set<EntityID>;
		set<EntityID> entities = new set<EntityID>;
		foreach(RplId id : m_mOwned[playerId])
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
			if(!rpl) continue;
			entities.Insert(rpl.GetEntity().GetID());
		}
		return entities;
	}
	
	set<EntityID> GetRented(string playerId)
	{
		if(!m_mRented.Contains(playerId)) return new set<EntityID>;
		set<EntityID> entities = new set<EntityID>;
		foreach(RplId id : m_mRented[playerId])
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
			if(!rpl) continue;
			entities.Insert(rpl.GetEntity().GetID());
		}
		return entities;
	}
	
	//RPC Methods
	
	override bool RplSave(ScriptBitWriter writer)
	{		
		
		//Send JIP owned
		writer.Write(m_mOwned.Count(), 32); 
		for(int i; i<m_mOwned.Count(); i++)
		{		
			set<RplId> ownedArray = m_mOwned.GetElement(i);
			RPL_WritePlayerID(writer, m_mOwned.GetKey(i));			
			writer.Write(ownedArray.Count(),32);
			for(int t; t<ownedArray.Count(); t++)
			{	
				writer.WriteRplId(ownedArray[t]);
			}
		}
		
		//Send JIP rented
		writer.Write(m_mRented.Count(), 32); 
		for(int i; i<m_mRented.Count(); i++)
		{		
			set<RplId> rentedArray = m_mRented.GetElement(i);
			RPL_WritePlayerID(writer, m_mRented.GetKey(i));			
			writer.Write(rentedArray.Count(),32);
			for(int t; t<rentedArray.Count(); t++)
			{	
				writer.WriteRplId(rentedArray[t]);
			}
		}
		
		return true;
	}
	
	override bool RplLoad(ScriptBitReader reader)
	{	
		
		int length, ownedlength;
		string playerId;
		RplId id;
			
		//Recieve JIP owned
		if (!reader.Read(length, 32)) return false;
		for(int i; i<length; i++)
		{
			if (!RPL_ReadPlayerID(reader, playerId)) return false;
			m_mOwned[playerId] = new set<RplId>;
			
			if (!reader.Read(ownedlength, 32)) return false;
			for(int t; t<ownedlength; t++)
			{
				if (!reader.ReadRplId(id)) return false;
				m_mOwned[playerId].Insert(id);
			}			
		}
		
		//Recieve JIP rented
		if (!reader.Read(length, 32)) return false;
		for(int i; i<length; i++)
		{
			if (!RPL_ReadPlayerID(reader, playerId)) return false;
			m_mRented[playerId] = new set<RplId>;
			
			if (!reader.Read(ownedlength, 32)) return false;
			for(int t; t<ownedlength; t++)
			{
				if (!reader.ReadRplId(id)) return false;
				m_mRented[playerId].Insert(id);
			}			
		}
		return true;
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetOwner(int playerId, RplId id)
	{
		DoSetOwner(playerId, id);	
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetRenter(int playerId, RplId id)
	{
		DoSetRenter(playerId, id);	
	}
	
	void DoSetOwner(int playerId, RplId id)
	{
		if(playerId == -1) {
			DoRemoveOwner(id);
		}else{
			string persId = OVT_Global.GetPlayers().GetPersistentIDFromPlayerID(playerId);
			DoSetOwnerPersistentId(persId, id);
		}		
	}
	
	void DoSetRenter(int playerId, RplId id)
	{
		if(playerId == -1) {
			DoRemoveRenter(id);
		}else{
			string persId = OVT_Global.GetPlayers().GetPersistentIDFromPlayerID(playerId);
			DoSetRenterPersistentId(persId, id);
		}
	}
	
	void DoRemoveOwner(RplId id)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
		if(!rpl) return;		
		
		string persId = GetOwnerID(rpl.GetEntity());
		int i = m_mOwned[persId].Find(id);
		if(i == -1) return;
		m_mOwned[persId].Remove(i);
		m_mOwners.Remove(id);
	}
	
	void DoRemoveRenter(RplId id)
	{
		RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
		if(!rpl) return;		
		
		string persId = GetOwnerID(rpl.GetEntity());
		int i = m_mRented[persId].Find(id);
		if(i == -1) return;
		m_mRented[persId].Remove(i);
		m_mRenters.Remove(id);
	}
	
	void DoSetOwnerPersistentId(string persId, RplId id)
	{
		if(!m_mOwned.Contains(persId)) m_mOwned[persId] = new set<RplId>;
		set<RplId> owner = m_mOwned[persId];
		owner.Insert(id);
		
		m_mOwners[id] = persId;
	}
	
	void DoSetRenterPersistentId(string persId, RplId id)
	{
		if(!m_mRented.Contains(persId)) m_mRented[persId] = new set<RplId>;
		set<RplId> owner = m_mRented[persId];
		owner.Insert(id);
		
		m_mRenters[id] = persId;
	}
	
	void ~OVT_OwnerManagerComponent()
	{
		if(m_mOwned)
		{
			m_mOwned.Clear();
			m_mOwned = null;
		}
		if(m_mRented)
		{
			m_mRented.Clear();
			m_mRented = null;
		}
		if(m_mOwners)
		{
			m_mOwners.Clear();
			m_mOwners = null;
		}
		if(m_mRenters)
		{
			m_mRenters.Clear();
			m_mRenters = null;
		}
	}
}