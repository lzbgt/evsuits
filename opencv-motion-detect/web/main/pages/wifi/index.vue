<template>
  <div style="width:220px;">
    <div>
    <nuxt-link to="/">返回主页</nuxt-link>
    <label>WIFI配置</label>
    <label>{{devSn}}</label>
    </div>
    <div>
        <div>
            <label>附近热点</label>
            <b-button size="sm" variant="primary" v-on:click="scanWifi">重新扫描</b-button>
            <div style="flex-direction:column;margin-top:10px">
                <b-button size="sm" variant="info" v-on:click="config"
                v-for=" item in ssids" style="width:200px;margin-bottom:10px" v-bind:key="item.id">
                    {{ item }}
                </b-button>
            </div>
            <b-modal hide-header-close no-close-on-esc no-close-on-backdrop hide-footer ref="config" id="config" size="sm" title="WIFI连接配置">
                <div>
                    <div>
                    <label disabled variant="info">热点名:</label>
                    <b-button disabled> {{ssid}}</b-button>
                    </div>

                    <div style="flex-direction:row;display:flex">
                    <label style="margin-right:10px">密码:</label>
                    <b-input  v-model="password" style="width:60%"> {{ssid}} </b-input>
                    </div>
                </div>
                <b-button :disabled="cancelDisabled" class="mt-3" variant="outline-danger" block @click="closeModal">取消</b-button>
                <b-button :disabled= "!connEnabled" class="mt-2" variant="outline-warning" block @click="connect">连接</b-button>
            </b-modal>
        </div>
        <div></div>
    </div>
  </div>
</template>

<script>
  import axios from 'axios'
  export default {
    async mounted (){
        try{
            this.getWifiData();
        }catch(err){
            console.log(err);
        }
    },

    data() {
      return {
        getWifiData: async ()=>{
            let response = await axios.get('http://192.168.1.104/wifi?scan=false')
            this.wifiData = response.data.wifiData
            this.ssids = Array.from(new Set(this.wifiData.wifi.ssids)).filter(e => e != "" && e != undefined).map(e => {
                    let data = e.match(/ESSID:\"(.+)\"/)[1];
                    if (data[0] != '\\') {
                        return data
                    }else{
                        var count = data.length;
                        var str = "";
                        
                        for(var index = 0; index < count; index += 1)
                            str += String.fromCharCode(data[index]);
                        return data
                    }
                }).filter(e => e[0]!='\\');
            this.devSn = this.wifiData.info.sn;
            console.log(this.devSn);
            console.log(this.ssids);
        },
        cancelDisabled: false,
        connDisabled: false,
        ssid: "",
        devSn:"",
        password: "",
        ssids: ["NO WIFI AVAILABLE"]
      }
    },
    computed: {
      connEnabled() {
          return (this.password.length >= 4) && !this.connDisabled;
      }
    },
    methods: {
        scanWifi: function(event){
            console.log("hello");
        },
        config: function(event){
            console.log(event.target.innerText);
            this.ssid = event.target.innerText;
            this.password = "";
            this.cancelDisabled = false;
            this.connDisabled = false;
            this.$bvModal.show('config');
        },
        closeModal(){
            this.$refs['config'].hide();
        },
        connect(){
            console.log(this.ssid, this.password, this.connEnabled);
            this.cancelDisabled = true;
            this.connDisabled = true;
        }
    }
  }
</script>
