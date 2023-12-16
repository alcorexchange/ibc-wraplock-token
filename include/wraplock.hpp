#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>

#include <string>

#include <bridge.hpp>
#include <eosio.token.hpp>

namespace eosiosystem {
   class system_contract;
}

namespace eosio {

   using std::string;

   class [[eosio::contract("wraplock")]] wraplock : public contract {
      private:

         // for bridge communication
         TABLE lpstruct {

            uint64_t id;

            bridge::lightproof lp;

            uint64_t primary_key()const { return id; }

            EOSLIB_SERIALIZE( lpstruct, (id)(lp) )

         } _light_proof_obj;

         TABLE hpstruct {

            uint64_t id;

            bridge::heavyproof hp;

            uint64_t primary_key()const { return id; }

            EOSLIB_SERIALIZE( hpstruct, (id)(hp) )

         } _heavy_proof_obj;

         using lptable = eosio::singleton<"lightproof"_n, lpstruct>;
         using hptable = eosio::singleton<"heavyproof"_n, hpstruct>;

         lptable _light_proof;
         hptable _heavy_proof;


         // structure used for globals - see `init` action for documentation
         struct [[eosio::table]] global {
            checksum256   chain_id;
            name          bridge_contract;
            checksum256   paired_chain_id;
            bool          enabled;
         } globalrow;

         // structure used for reserve account balances, scoped by token contract
         struct [[eosio::table]] account {
            asset    balance;

            uint64_t primary_key()const { return balance.symbol.code().raw(); }
         };

         // structure used for mapping between native token contracts and wrapped token contracts
         struct [[eosio::table]] contract_mapping {
            name    native_token_contract;
            name    paired_wraptoken_contract;

            uint64_t primary_key()const { return native_token_contract.value; }
            uint64_t by_paired_wraptoken_contract()const { return paired_wraptoken_contract.value; }
         };

         // structure used for retaining action receipt digests of accepted proven actions, to prevent replay attacks
         struct [[eosio::table]] processed {

           uint64_t                        id;
           checksum256                     receipt_digest;

           uint64_t primary_key()const { return id; }
           checksum256 by_digest()const { return receipt_digest; }

         };

         void sub_reserve(const extended_asset& value );
         void add_reserve(const extended_asset& value );
         void add_or_assert(const bridge::actionproof& actionproof, const name& payer);
         void _withdraw(const name& prover, const bridge::actionproof actionproof);
         void _cancel(const name& prover, const bridge::actionproof actionproof);

      public:
         using contract::contract;

         // structure used for the `emitxfer` action used in proof on wrapped token chain
         struct [[eosio::table]] xfer {
           name             owner;
           extended_asset   quantity;
           name             beneficiary;
           string           memo;
         };

         /**
          * Allows contract account to set which chains and associated bridge contracts are used for interchain transfers.
          *
          * @param chain_id - the id of the chain running this contract
          * @param bridge_contract - the bridge contract on this chain
          * @param paired_chain_id - the id of the chain hosting the wrapped tokens
          */
         [[eosio::action]]
         void init(const checksum256& chain_id, const name& bridge_contract, const checksum256& paired_chain_id);

         /**
          * Allows contract account to add support for an asset contract for interchain transfers.
          *
          * @param native_token_contract - the token contract being enabled for interchain transfers
          * @param paired_wraptoken_contract - the corresponding wraptoken contract which transfers are sent to/from
          */
         [[eosio::action]]
         void addcontract(const name& native_token_contract, const name& paired_wraptoken_contract);

         /**
          * Allows contract account to disable support for an asset contract for interchain transfers.
          *
          * @param native_token_contract - the token contract being disabled for interchain transfers
          */
         [[eosio::action]]
         void delcontract(const name& native_token_contract);

         /**
          * Allows `prover` account to redeem native tokens and send them to the beneficiary indentified in the `actionproof`.
          *
          * @param prover - the calling account whose ram is used for storing the action receipt digest to prevent replay attacks
          * @param blockproof - the heavy proof data structure
          * @param actionproof - the proof structure for the `emitxfer` action associated with the `retire` action on the wrapped tokens chain
          */
         [[eosio::action]]
         void withdrawa(const name& prover, const bridge::heavyproof blockproof, const bridge::actionproof actionproof);

         /**
          * Allows `prover` account to redeem native tokens and send them to the beneficiary indentified in the `actionproof`.
          *
          * @param prover - the calling account whose ram is used for storing the action receipt digest to prevent replay attacks
          * @param blockproof - the light proof data structure
          * @param actionproof - the proof structure for the `emitxfer` action associated with the `retire` action on the wrapped tokens chain
          */
         [[eosio::action]]
         void withdrawb(const name& prover, const bridge::lightproof blockproof, const bridge::actionproof actionproof);
      
         /**
          * Allows `prover` account to cancel a token transfer and return them to the beneficiary indentified in the `actionproof`.
          *
          * @param prover - the calling account whose ram is used for storing the action receipt digest to prevent replay attacks
          * @param blockproof - the heavy proof data structure
          * @param actionproof - the proof structure for the `emitxfer` action associated with the retiring transfer action on the native chain
          */
         [[eosio::action]]
         void cancela(const name& prover, const bridge::heavyproof blockproof, const bridge::actionproof actionproof);

         /**
          * Allows `prover` account to cancel a token transfer and return them to the beneficiary indentified in the `actionproof`.
          *
          * @param prover - the calling account whose ram is used for storing the action receipt digest to prevent replay attacks
          * @param blockproof - the light proof data structure
          * @param actionproof - the proof structure for the `emitxfer` action associated with the retiring transfer action on the native chain
          */
         [[eosio::action]]
         void cancelb(const name& prover, const bridge::lightproof blockproof, const bridge::actionproof actionproof);

         /**
          * The inline action created by this contract when tokens are locked. Proof of this action is used on the wrapped token chain.
          */
         [[eosio::action]]
         void emitxfer(const wraplock::xfer& xfer);

         /**
          * Disable all user actions on the contract.
          */
         [[eosio::action]]
         void disable();

         /**
          * Enable all user actions on the contract.
          */
         [[eosio::action]]
         void enable();
         
         /**
          * Allows contract account to clear existing state except which chains and associated contracts are used.
          */
         //[[eosio::action]]
         //void clear();

         /**
          * On transfer notification, calls the deposit function which locks the `quantity` of tokens sent in the reserve and calls
          * the `emitxfer` action inline so that can be used as the basis for a proof of locking for the issue/cancel actions
          * on the wrapped token chain.
          *
          * @param from - the owner of the tokens to be sent to the wrapped token chain
          * @param to - this contract account
          * @param quantity - the asset to be sent to the wrapped token chain
          * @param memo - the beneficiary account on the wrapped token chain
          */
         [[eosio::on_notify("*::transfer")]] void deposit(name from, name to, asset quantity, string memo);

         using transfer_action = action_wrapper<"transfer"_n, &token::transfer>;
         using heavyproof_action = action_wrapper<"checkproofb"_n, &bridge::checkproofb>;
         using lightproof_action = action_wrapper<"checkproofc"_n, &bridge::checkproofc>;
         using emitxfer_action = action_wrapper<"emitxfer"_n, &wraplock::emitxfer>;

         typedef eosio::multi_index< "reserves"_n, account > reserves;
         typedef eosio::multi_index< "contractmap"_n, contract_mapping,
            indexed_by<"wraptoken"_n, const_mem_fun<contract_mapping, uint64_t, &contract_mapping::by_paired_wraptoken_contract>> > contractmapping;
      
         typedef eosio::multi_index< "processed"_n, processed,
            indexed_by<"digest"_n, const_mem_fun<processed, checksum256, &processed::by_digest>>> processedtable;

         using globaltable = eosio::singleton<"global"_n, global>;

         globaltable global_config;

         processedtable _processedtable;
         contractmapping _contractmappingtable;

         wraplock( name receiver, name code, datastream<const char*> ds ) :
         contract(receiver, code, ds),
         global_config(_self, _self.value),
         _processedtable(_self, _self.value),
         _contractmappingtable(_self, _self.value),
         _light_proof(receiver, receiver.value),
         _heavy_proof(receiver, receiver.value)
         {

         }
        
   };

}

