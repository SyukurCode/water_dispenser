//led
const clientid = 'web' + getTimestampInSeconds();
$( function() {
var $winHeight = $( window ).height()
$( '.container' ).height( $winHeight );
});
//led
var capacityRequest = 250;
var client = new Paho.MQTT.Client("myqs-bunker.ddns.net", Number(80), clientid);
var glassWeight = 0;


//water fill
var fm3 = new FluidMeter();
fm3.init({
	targetContainer: document.getElementById("fluid-meter-3"),
	fillPercentage: 0,
	options: {
		fontSize: "30px",
		drawPercentageSign: true,
		drawBubbles: true,
		size: 200,
		borderWidth: 5,
		backgroundColor: "#e2e2e2",
		foregroundColor: "#41D2E9",
		foregroundFluidLayer: {
			fillStyle: "#16E1FF",
			angularSpeed: 30,
			maxAmplitude: 5,
			frequency: 30,
			horizontalSpeed: -200
		},
		backgroundFluidLayer: {
			fillStyle: "#4F8FC6",
			angularSpeed: 100,
			maxAmplitude: 3,
			frequency: 22,
			horizontalSpeed: 200
		}	
	}
});

// set callback handlers
client.onConnectionLost = onConnectionLost;
client.onMessageArrived = onMessageArrived;

// connect the client
client.connect({ 
	onSuccess: onConnect,
	userName: "syukur",
	password: "syukur123***"
});
function getTimestampInSeconds() {
return Math.floor(Date.now() / 1000)
}
// called when the client connects
function onConnect() {
	// Once a connection has been made, make a subscription and send a message.
	console.log("onConnect");
	client.subscribe("web");
	startUp();
}

// called when the client loses its connection
function onConnectionLost(responseObject) {
	if (responseObject.errorCode !== 0) {
		console.log("onConnectionLost:" + responseObject.errorMessage);
		disabledAll();
		ledRed("off");
	}
}

// called when a message arrives
function onMessageArrived(message) {
	console.log("New message: " + message.payloadString);
	let text = message.payloadString;
	const myArray = text.split("/");
	let arraySize = myArray.length;
	console.log("Event: " + myArray[1]);
	console.log("Value: " + myArray[2]);
	if(arraySize > 3) 
	{
		console.log("Type: " + myArray[4]);
		console.log("Request: " + myArray[6]);				
	}
	console.log("Len: " + arraySize);
	
	if(myArray[1] == "status")
	{
		if(myArray[2] == "online")
		{
			ledRed("on");
			sendMessage("waterdispenser","/check/0");
		}
		if(myArray[2] == "stop")
		{
			ledYellow("off");
			sendMessage("waterdispenser","/check/0");
		}
	}
	if(myArray[1] == "water")
	{
		if(myArray[2] == 0)
		{
			ledBlue("off");
		}
		if(myArray[2] == 1)
		{
			ledBlue("on");
		}
	}
	if(myArray[1] == "filling")
	{
		ledYellow("on");
		let volumereq = 0;
		let capacity = 0;
		let offset = 0.0;
		var percent = myArray[2];
		if(myArray.length > 2)
		{
			volumereq = parseFloat(myArray[6]) + offset;
			dispenseSpeed = parseFloat(myArray[8]);
			capacity = percent/100 * volumereq;
			document.getElementById("type").innerHTML = myArray[4];
			document.getElementById("request").innerHTML = volumereq.toString();
		}
		if(percent > -1 && percent < 101.0)
		{
			fm3.setPercentage(percent);
			disabledAll();
			document.getElementById("capacity").innerHTML = capacity.toString();
			document.getElementById("btnStop").disabled = false;
		}
		document.getElementById("dispenseSpeed").innerHTML = dispenseSpeed.toString();
	}
	if(myArray[1] == "status" && myArray[2] == "noglass")
	{
		disabledAll();
		ledYellow("off");
		fm3.setPercentage(0);
		document.getElementById("capacity").innerHTML = "0";
		document.getElementById("dispenseSpeed").innerHTML = "0";
	}
	if(myArray[1] == "status" && myArray[2] == "ready")
	{
		ledYellow("off");
		enabledAll()
	}
}
function sendMessage(destination,msg)
{
	message = new Paho.MQTT.Message(msg);
	message.destinationName = destination;
	client.send(message);
}
function ledRed(state)
{
	const element = document.getElementById("ledStatus");
	if(state == "on") 
	{
		element.style.backgroundColor = "#F00";
		element.style.boxShadow = "rgba(0, 0, 0, 0.2) 0 -1px 7px 1px, inset #441313 0 -1px 9px, rgba(255, 0, 0, 0.5) 0 2px 12px";
	}
	else
	{
		element.style.backgroundColor = "#A00";
		element.style.boxShadow = "rgba(0, 0, 0, 0.2) 0 -1px 7px 1px, inset #441313 0 -1px 9px, rgba(255, 0, 0, 0.5) 0 2px 0px";
	}
}
function ledGreen(state)
{
	const element = document.getElementById("ledReady");
	if(state == "on") 
	{
		element.style.backgroundColor = "#ABFF00";
		element.style.boxShadow = "box-shadow: rgba(0, 0, 0, 0.2) 0 -1px 7px 1px, inset #304701 0 -1px 9px, #89FF00 0 2px 12px;";
	}
	else
	{
		element.style.backgroundColor = "#144C05";
		element.style.boxShadow = "box-shadow: rgba(0, 0, 0, 0.2) 0 -1px 7px 1px, inset #304701 0 -1px 9px, #89FF00 0 2px 0px;";
	}
}
function ledYellow(state)
{
	
	const element = document.getElementById("ledFilling");
	if(state == "on") 
	{
		element.style.backgroundColor = "#FF0";
		element.style.boxShadow = "box-shadow: rgba(0, 0, 0, 0.2) 0 -1px 7px 1px, inset #808002 0 -1px 9px, #89FF00 0 2px 12px;";
	}
	else
	{
		element.style.backgroundColor = "#AA0";
		element.style.boxShadow = "box-shadow: rgba(0, 0, 0, 0.2) 0 -1px 7px 1px, inset #808002 0 -1px 9px, #FF0 0 2px 0px;";
	}
}
function ledBlue(state)
{
	
	const element = document.getElementById("ledWater");
	if(state == "on") 
	{
		element.style.backgroundColor = "#24E0FF";
		element.style.boxShadow = "box-shadow: rgba(0, 0, 0, 0.2) 0 -1px 7px 1px, inset #006 0 -1px 9px, #3F8CFF 0 2px 14px;";
	}
	else
	{
		element.style.backgroundColor = "#135A6A";
		element.style.boxShadow = "box-shadow: rgba(0, 0, 0, 0.2) 0 -1px 7px 1px, inset #006 0 -1px 9px, #3F8CFF 0 2px 0px;";
	}
}
function startUp()
{
	sendMessage("waterdispenser","/status/0");
}
function disabledAll()
{
	ledGreen("off");
	document.getElementById("btnNormal").disabled = true;
	document.getElementById("btnWarm").disabled = true;
	document.getElementById("btnHot").disabled = true;
	document.getElementById("flexRadioDefault1").disabled = true;
	document.getElementById("flexRadioDefault2").disabled = true;
	document.getElementById("flexRadioDefault3").disabled = true;
	document.getElementById("flexRadioDefault4").disabled = true;
	document.getElementById("flexRadioDefault5").disabled = true;
}	
function enabledAll()
{
	document.getElementById("btnNormal").disabled = false;
	document.getElementById("btnWarm").disabled = false;
	document.getElementById("btnHot").disabled = false;
	document.getElementById("flexRadioDefault1").disabled = false;
	document.getElementById("flexRadioDefault2").disabled = false;
	document.getElementById("flexRadioDefault3").disabled = false;
	document.getElementById("flexRadioDefault4").disabled = false;
	document.getElementById("flexRadioDefault5").disabled = false;
	ledGreen("on");
	document.getElementById("btnStop").disabled = true;
}
function volumeSet(ml)
{
	console.log(ml);
	/* console.log(active); */
	capacityRequest = ml;
}
document.getElementById("btnNormal").addEventListener("click", function (event) {
	if(capacityRequest > 2500)
	{
		alert("Hello! limit 2500ml je ok!!!");
	}
	else if(capacityRequest < 100)
	{
		alert("Terlalu sikit volume minima yang dibenarkan 100ml");
	}
	else
	{
		sendMessage("waterdispenser","/normal/" + capacityRequest);
		disabledAll();
	}
});
document.getElementById("btnWarm").addEventListener("click", function (event) {
	if(capacityRequest > 2500)
	{
		alert("Hello! limit 2500ml je ok!!!");
	}
	else if(capacityRequest < 100)
	{
		alert("Terlalu sikit volume minima yang dibenarkan 100ml");
	}
	else
	{
		sendMessage("waterdispenser","/warm/" + capacityRequest);
		disabledAll();
	}
});
document.getElementById("btnHot").addEventListener("click", function (event) {
	if(capacityRequest > 2500)
	{
		alert("Hello! limit 2500ml je ok!!!");
	}
	else if(capacityRequest < 100)
	{
		alert("Terlalu sikit volume minima yang dibenarkan 100ml");
	}
	else
	{
		sendMessage("waterdispenser","/hot/" + capacityRequest);
		disabledAll();
	}
});
document.getElementById("btnStop").addEventListener("click", function (event) {
	sendMessage("waterdispenser","/stop/0");
	document.getElementById("btnStop").disabled = true;
});
/* if(/Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent)){
	const isPortrait = window.matchMedia('(orientation: portrait)').matches;
	if(isPortrait)
	{
		document.getElementById("screen").style.transform = "rotate(90deg)";
	} 
}*/
