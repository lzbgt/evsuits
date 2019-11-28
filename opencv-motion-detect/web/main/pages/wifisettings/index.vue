<template>
  <div class="grid">
    <div class="row">
      <nuxt-link to="/" class="col-4">返回主页</nuxt-link>
      <label class="col-4">WIFI配置</label>
      <label class="col-4">{{devSn}}</label>
    </div>
    <div class="row">
      <div class="alert offset-2 col-8" :class="alertType">当前连接WIFI: {{ssidActive}}</div>
    </div>
    <div class="row">
      <div class="alert col-6" :class="alertType">MAC {{mac}}</div>
      <div class="alert col-6" :class="alertType">IP {{ip}}</div>
    </div>

    <div class="row">
      <label class="col-4 offset-2">附近热点</label>
      <b-button :disabled="bInScan" class="col-4" variant="primary" v-on:click="scanWifi">刷新</b-button>
    </div>

    <div class="row">
      <b-button
        class="col-10 offset-1 mt-3"
        variant="primary"
        v-on:click="config"
        v-for=" item in ssids"
        v-bind:key="item.id"
      >{{ item }}</b-button>
    </div>

    <b-modal
      hide-header-close
      no-close-on-esc
      no-close-on-backdrop
      hide-footer
      ref="config"
      id="config"
      size="mw-100"
      title="WIFI连接配置"
    >
      <div class="grid">
        <div class="row">
          <label class="col-3 offset-1" disabled variant="info">热点名</label>
          <b-button class="col-7" disabled>{{ssid}}</b-button>
        </div>
        <div class="row mt-4">
          <label class="col-2 offset-1">密码</label>
          <b-input class="col-8" v-model="password">{{ssid}}</b-input>
        </div>
        <div class="row mt-4">
          <b-button
            :disabled="cancelDisabled"
            class="col-2 offset-3"
            variant="outline-danger"
            @click="closeModal"
          >取消</b-button>
          <b-button
            :disabled="!connEnabled"
            class="col-2 offset-2"
            variant="primary"
            @click="connect"
          >连接</b-button>
        </div>
      </div>
    </b-modal>
  </div>
</template>

<script>
import axios from "axios";
const apiHost = ''; //'http://192.168.1.104';
export default {
  async mounted() {
    try {
      this.getWifiData();
    } catch (err) {
      console.log(err);
    }
  },

  data() {
    return {
      connectWifi: async e => {
          let response = await axios.get(`${apiHost}/wifi?mode=2&ssid=${this.ssid}&password=${this.password}`);
          return response;
      },
      getWifiData: async e => {
        this.bInScan = true;
        let param = e ? "true" : "false";
        let response = await axios.get(
          `${apiHost}/wifi?scan=${param}`
        );
        this.wifiData = response.data.wifiData;
        this.ssids = Array.from(new Set(this.wifiData.wifi.ssids))
          .filter(e => e != "" && e != undefined)
          .map(e => {
            try{
              let data = e.match(/ESSID:\"(.+)\"/)[1];
              if (data[0] != "\\") {
                return data;
              } else {
                var count = data.length;
                var str = "";

                for (var index = 0; index < count; index += 1)
                  str += String.fromCharCode(data[index]);
                return data;
              }
            }catch(error){
              console.log(error);
              return '';
            }
          })
          .filter(e => e !='' && e[0] != "\\");
        this.devSn = this.wifiData.info.sn;
        this.bInScan = false;
        console.log(this.devSn);
        console.log(this.wifiData);
      },
      cancelDisabled: false,
      connDisabled: false,
      bInScan: false,
      ssid: "",
      devSn: "",
      password: "",
      ssids: ["NO WIFI AVAILABLE"],
      wifiData: undefined
    };
  },
  computed: {
    connEnabled() {
      return this.password.length >= 4 && !this.connDisabled;
    },
    connected(){
      return ((this.wifiData||{}).wifi||{}).ssid||false;
    },
    mac(){
      return ((this.wifiData||{}).wifi||{}).mac||"";
    },
    ip() {
      return ((this.wifiData||{}).wifi||{}).ip||"";
    },
    ssidActive() {
      return ((this.wifiData||{}).wifi||{}).ssid||"<无>";
    },
    alertType(){
      if(this.connected){
        return "alert-success";
      }else{
        return "alert-danger";
      }
    }
  },
  methods: {
    scanWifi: function(event) {
      this.getWifiData(true);
    },
    config: function(event) {
      console.log(event.target.innerText);
      this.ssid = event.target.innerText;
      this.password = "";
      this.cancelDisabled = false;
      this.connDisabled = false;
      this.$bvModal.show("config");
    },
    closeModal() {
      this.$refs["config"].hide();
    },
    connect() {
      this.connectWifi();
      this.cancelDisabled = true;
      this.connDisabled = true;
    }
  }
};
</script>
