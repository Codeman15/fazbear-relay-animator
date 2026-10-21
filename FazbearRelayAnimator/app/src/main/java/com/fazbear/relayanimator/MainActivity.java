package com.fazbear.relayanimator;

import android.app.Activity;
import android.os.Bundle;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.widget.*;
import org.json.*;
import java.net.*;
import java.io.*;
import java.util.concurrent.*;

public class MainActivity extends Activity {
    EditText ipBox, tokenBox, nameBox, durationBox, timeBox, relayBox;
    LinearLayout root, eventList;
    TextView status;
    JSONArray events = new JSONArray();
    ExecutorService io = Executors.newSingleThreadExecutor();
    String stageBase() { String ip=ipBox.getText().toString().trim(); if(ip.startsWith("http://")) return ip; return "http://"+ip; }
    String token() { return tokenBox.getText().toString().trim(); }
    void request(String method, String path, JSONObject body, java.util.function.Consumer<String> done) {
        io.execute(() -> {
            String result; HttpURLConnection c=null;
            try {
                URL u=new URL(stageBase()+path); c=(HttpURLConnection)u.openConnection(); c.setRequestMethod(method); c.setConnectTimeout(3500); c.setReadTimeout(5000);
                c.setRequestProperty("X-Fazbear-Token", token());
                if(body!=null){c.setDoOutput(true);c.setRequestProperty("Content-Type","application/json");try(OutputStream o=c.getOutputStream()){o.write(body.toString().getBytes("UTF-8"));}}
                InputStream in=(c.getResponseCode()<400)?c.getInputStream():c.getErrorStream();
                ByteArrayOutputStream out=new ByteArrayOutputStream(); byte[] b=new byte[1024]; int n; while((n=in.read(b))>0)out.write(b,0,n);
                result=c.getResponseCode()+" "+out.toString("UTF-8");
            } catch(Exception e){result="Connection error: "+e.getMessage();}
            finally {if(c!=null)c.disconnect();}
            final String r=result; runOnUiThread(()->done.accept(r));
        });
    }
    @Override public void onCreate(Bundle b){super.onCreate(b); buildUi();}
    TextView text(String s,int size){TextView t=new TextView(this);t.setText(s);t.setTextSize(size);t.setTextColor(Color.WHITE);t.setPadding(8,8,8,8);return t;}
    Button button(String s,Runnable r){Button b=new Button(this);b.setText(s);b.setOnClickListener(v->r.run());return b;}
    void buildUi(){
        ScrollView sc=new ScrollView(this); root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(16,16,16,24);root.setBackgroundColor(Color.rgb(18,18,22));sc.addView(root);setContentView(sc);
        root.addView(text("FAZBEAR RELAY ANIMATOR",24));
        root.addView(text("Stage connection",18));
        ipBox=new EditText(this);ipBox.setSingleLine(true);ipBox.setHint("Stage IP address, e.g. 192.168.1.50");ipBox.setTextColor(Color.WHITE);root.addView(ipBox);
        tokenBox=new EditText(this);tokenBox.setSingleLine(true);tokenBox.setHint("Stage API token");tokenBox.setTextColor(Color.WHITE);root.addView(tokenBox);
        status=text("Not connected",14);root.addView(status);
        root.addView(button("CHECK STAGE CONNECTION",()->request("GET","/api/v1/status",null,s->status.setText(s))));
        root.addView(text("Manual relay test",18));
        for(int i=1;i<=8;i++){final int relay=i;root.addView(button("TOGGLE RELAY "+i,()->{
            JSONObject j=new JSONObject(); try{j.put("state",true);}catch(Exception ignored){}
            request("POST","/api/v1/relay/"+relay,j,s->status.setText("Relay "+relay+": "+s));
        }));}
        root.addView(button("EMERGENCY STOP — ALL RELAYS OFF",()->request("POST","/api/v1/emergency-stop",new JSONObject(),s->status.setText(s))));
        root.addView(text("Sequence editor (events are saved to Stage)",18));
        nameBox=new EditText(this);nameBox.setHint("Sequence ID (letters/numbers/-/_)");nameBox.setTextColor(Color.WHITE);nameBox.setText("stage-lights-01");root.addView(nameBox);
        durationBox=new EditText(this);durationBox.setHint("Duration in milliseconds");durationBox.setInputType(2);durationBox.setTextColor(Color.WHITE);durationBox.setText("5000");root.addView(durationBox);
        LinearLayout line=new LinearLayout(this);line.setOrientation(LinearLayout.HORIZONTAL);
        timeBox=new EditText(this);timeBox.setHint("Time ms");timeBox.setInputType(2);timeBox.setTextColor(Color.WHITE);timeBox.setText("500");
        relayBox=new EditText(this);relayBox.setHint("Relay 1-8");relayBox.setInputType(2);relayBox.setTextColor(Color.WHITE);relayBox.setText("1");
        line.addView(timeBox,new LinearLayout.LayoutParams(0,-2,1));line.addView(relayBox,new LinearLayout.LayoutParams(0,-2,1));root.addView(line);
        root.addView(button("ADD EVENT: RELAY ON",()->addEvent(true)));
        root.addView(button("ADD EVENT: RELAY OFF",()->addEvent(false)));
        eventList=new LinearLayout(this);eventList.setOrientation(LinearLayout.VERTICAL);root.addView(eventList);
        root.addView(button("UPLOAD / SAVE SEQUENCE TO STAGE",this::saveSequence));
        root.addView(button("PLAY SAVED SEQUENCE",()->request("POST","/api/v1/sequences/"+nameBox.getText().toString().trim()+"/play",new JSONObject(),s->status.setText(s))));
        root.addView(button("STOP SEQUENCE",()->request("POST","/api/v1/sequences/stop",new JSONObject(),s->status.setText(s))));
        root.addView(text("Use only on your trusted local Wi-Fi. Confirm relay polarity with loads disconnected.",12));
    }
    void addEvent(boolean on){
        try{
            int time=Integer.parseInt(timeBox.getText().toString().trim());
            int relay=Integer.parseInt(relayBox.getText().toString().trim());
            if(time<0||relay<1||relay>8){status.setText("Time must be >= 0 and relay must be 1-8");return;}
            JSONObject e=new JSONObject();e.put("timeMs",time);e.put("relay",relay);e.put("state",on);events.put(e);
            TextView row=text(time+" ms  •  Relay "+relay+"  •  "+(on?"ON":"OFF"),14);eventList.addView(row);
        }catch(Exception e){status.setText("Enter a valid time and relay number");}
    }
    void saveSequence(){
        try{
            JSONObject s=new JSONObject();s.put("id",nameBox.getText().toString().trim());s.put("name",s.getString("id"));s.put("durationMs",Integer.parseInt(durationBox.getText().toString().trim()));
            JSONArray initial=new JSONArray();for(int i=0;i<8;i++)initial.put(false);s.put("initialStates",initial);s.put("events",events);s.put("endBehavior","ALL_OFF");
            request("PUT","/api/v1/sequences",s,r->status.setText(r));
        }catch(Exception e){status.setText("Sequence error: "+e.getMessage());}
    }
}
