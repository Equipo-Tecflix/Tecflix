const {defineConfig}=require('@playwright/test');
module.exports=defineConfig({testDir:'.',timeout:180000,workers:1,use:{browserName:'chromium'},reporter:'list'});
