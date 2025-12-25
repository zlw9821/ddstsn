
package apps;

simple DdsPublisherModule
{
    @class(DdsPublisherModule);
}


simple DdsSubscriberModule
{
    @class(DdsSubscriberModule);
}


network FastDDSDemo
{
    submodules:
        pub: DdsPublisherModule {
            @display("p=100,100");
        }
        sub: DdsSubscriberModule {
            @display("p=300,100");
        }
}
