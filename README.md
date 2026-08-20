# master


#### How to run the 5G testbed virtually
To use Terraform with NREC we must set up a SSH key and provide the Terraform some ENV's.  

##### SSH key

##### Variables
Add a ```keystone_rc.sh``` file as described in [link](https://docs.nrec.no/api.html)

```
export OS_USERNAME=<feide-id>
export OS_PROJECT_NAME=<project>
export OS_PASSWORD=<password>
export OS_AUTH_URL=https://api.nrec.no:5000/v3
export OS_IDENTITY_API_VERSION=3
export OS_USER_DOMAIN_NAME=dataporten
export OS_PROJECT_DOMAIN_NAME=dataporten
export OS_REGION_NAME=<region>
export OS_INTERFACE=public
export OS_NO_CACHE=1
```

and always ```source keystone_rc.sh```before running Terraform. 

Run: 
```
cd ./terraform
terraform init
terraform apply
```

The VM's should be running! Double check with 

```openstack server list```